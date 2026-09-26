#include <pthread.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <array>
#include <atomic>
#include <cstring>
#include "host/ntmachost/handles.h"
#include "host/runtime/process_env.h"
#include "host/runtime/thunks.h"
#include "host/loader/exceptions/load_error.h"
#include "purefuncs.h"

namespace ntmac::runtime {

namespace {

thread_local ntmac_teb* tls_current_teb = nullptr;

// シグナルハンドラから参照するため，thread_local ではなく固定長の配列で持つ
std::array<std::atomic<const void*>, 256> known_tebs{};

void* alloc_zeroed(size_t size) {
    void* p = mmap(nullptr, align_up(size, getpagesize()), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) throw loader::LoadError("メモリを確保できませんでした");
    return p;
}

uint64_t read_gs(uint64_t offset) {
    uint64_t v;
    __asm__ volatile("movq %%gs:(%1), %0" : "=r"(v) : "r"(offset));
    return v;
}

/**
 * このスレッドの macOS TSD (ホストでの gs ベース) を求めます．
 * 取得する API が無いため，TSD の先頭スロット (pthread_self) と一致する位置を pthread_t の中から探します．
 */
void* find_host_tsd() {
    auto self = reinterpret_cast<uint8_t*>(pthread_self());
    for (size_t off = 0; off < 0x1000; off += sizeof(uint64_t)) {
        auto slots = reinterpret_cast<const uint64_t*>(self + off);
        if (slots[0] == reinterpret_cast<uint64_t>(self) && slots[1] == read_gs(8) && slots[3] == read_gs(24)) {
            return self + off;
        }
    }
    return nullptr;
}

void register_teb(const void* teb) {
    for (auto& slot : known_tebs) {
        const void* expected = nullptr;
        if (slot.compare_exchange_strong(expected, teb)) return;
    }
    throw loader::LoadError("TEB の登録数が上限を超えました");
}

/**
 * UTF-8 を UTF-16 に変換し，終端付きで確保した UNICODE_STRING にします．
 */
ntmac_unicode_string make_unicode_string(const std::string& s) {
    std::vector<uint16_t> out;
    for (size_t i = 0; i < s.size();) {
        auto c = static_cast<unsigned char>(s[i]);
        uint32_t cp;
        size_t n;
        if (c < 0x80)              { cp = c;        n = 1; }
        else if ((c >> 5) == 0x6)  { cp = c & 0x1F; n = 2; }
        else if ((c >> 4) == 0xE)  { cp = c & 0x0F; n = 3; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; n = 4; }
        else                       { cp = 0xFFFD;   n = 1; }
        if (i + n > s.size()) { cp = 0xFFFD; n = s.size() - i; }
        for (size_t k = 1; k < n; k++) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += n;

        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<uint16_t>(0xD800 | (cp >> 10)));
            out.push_back(static_cast<uint16_t>(0xDC00 | (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<uint16_t>(cp));
        }
    }

    size_t bytes = out.size() * sizeof(uint16_t);
    if (bytes + sizeof(uint16_t) > UINT16_MAX) {
        throw loader::LoadError("文字列が長すぎます");
    }
    auto buf = static_cast<uint16_t*>(alloc_zeroed(bytes + sizeof(uint16_t)));
    std::memcpy(buf, out.data(), bytes);

    ntmac_unicode_string us{};
    us.Length = static_cast<uint16_t>(bytes);
    us.MaximumLength = static_cast<uint16_t>(bytes + sizeof(uint16_t));
    us.Buffer = buf;
    return us;
}

/**
 * Windows のコマンドライン規則に従って引数を連結します．
 */
std::string build_command_line(const std::vector<std::string>& args) {
    std::string line;
    for (const auto& arg : args) {
        if (!line.empty()) line += ' ';
        if (!arg.empty() && arg.find_first_of(" \t\"") == std::string::npos) {
            line += arg;
            continue;
        }

        line += '"';
        size_t backslashes = 0;
        for (char c : arg) {
            if (c == '\\') {
                backslashes++;
            } else if (c == '"') {
                line.append(backslashes * 2 + 1, '\\');
                line += c;
                backslashes = 0;
            } else {
                line.append(backslashes, '\\');
                line += c;
                backslashes = 0;
            }
        }
        line.append(backslashes * 2, '\\');
        line += '"';
    }
    return line;
}

uint32_t cpu_count() {
    int n = 0;
    size_t len = sizeof(n);
    if (sysctlbyname("hw.logicalcpu", &n, &len, nullptr, 0) != 0 || n <= 0) return 1;
    return static_cast<uint32_t>(n);
}

}

std::string check_gs_switch() {
    void* tsd = find_host_tsd();
    if (!tsd) return "ホストの TSD を特定できませんでした";

    auto teb = static_cast<ntmac_teb*>(alloc_zeroed(sizeof(ntmac_teb)));
    teb->Self = teb;
    teb->HostTsd = tsd;

    // gs を切り替えた状態で gs:[0x30] を読み，TEB 自身のアドレスが返るかを見る
    auto seen = ntmac_call_pe(teb, reinterpret_cast<const void*>(&ntmac_read_gs_self));
    bool host_ok = read_gs(0) == reinterpret_cast<uint64_t>(pthread_self());
    munmap(teb, align_up(sizeof(ntmac_teb), getpagesize()));

    if (seen != reinterpret_cast<uint64_t>(teb)) return "gs ベースを TEB に切り替えられませんでした";
    if (!host_ok) return "gs ベースをホストに戻せませんでした";
    return {};
}

ntmac_peb* create_peb(const PebParams& params) {
    auto peb = static_cast<ntmac_peb*>(alloc_zeroed(sizeof(ntmac_peb)));
    auto pp = static_cast<ntmac_process_parameters*>(alloc_zeroed(sizeof(ntmac_process_parameters)));

    pp->MaximumLength = sizeof(ntmac_process_parameters);
    pp->Length = sizeof(ntmac_process_parameters);
    pp->StandardInput = reinterpret_cast<void*>(ntmachost::std_handle(STDIN_FILENO));
    pp->StandardOutput = reinterpret_cast<void*>(ntmachost::std_handle(STDOUT_FILENO));
    pp->StandardError = reinterpret_cast<void*>(ntmachost::std_handle(STDERR_FILENO));
    pp->ImagePathName = make_unicode_string(params.image_path);
    pp->CommandLine = make_unicode_string(build_command_line(params.args));

    peb->ImageBaseAddress = params.image_base;
    peb->ProcessParameters = pp;
    peb->NumberOfProcessors = cpu_count();
    peb->OSMajorVersion = 10;
    peb->OSMinorVersion = 0;
    peb->OSBuildNumber = 19045;
    peb->OSPlatformId = 2; // VER_PLATFORM_WIN32_NT
    peb->ImageSubsystem = params.subsystem;
    peb->ImageSubsystemMajorVersion = params.subsystem_major;
    peb->ImageSubsystemMinorVersion = params.subsystem_minor;
    return peb;
}

ntmac_teb* create_teb(ntmac_peb* peb) {
    void* tsd = find_host_tsd();
    if (!tsd) throw loader::LoadError("ホストの TSD を特定できませんでした");

    auto teb = static_cast<ntmac_teb*>(alloc_zeroed(sizeof(ntmac_teb)));
    auto thread = pthread_self();
    auto stack_top = static_cast<uint8_t*>(pthread_get_stackaddr_np(thread));
    auto stack_bottom = stack_top - pthread_get_stacksize_np(thread);

    uint64_t tid = 0;
    pthread_threadid_np(thread, &tid);

    teb->StackBase = stack_top;
    teb->StackLimit = stack_bottom;
    teb->DeallocationStack = stack_bottom;
    teb->Self = teb;
    teb->UniqueProcess = static_cast<uint64_t>(getpid());
    teb->UniqueThread = tid;
    teb->ProcessEnvironmentBlock = peb;
    teb->HostTsd = tsd;

    register_teb(teb);
    tls_current_teb = teb;
    return teb;
}

ntmac_teb* current_teb() {
    return tls_current_teb;
}

bool is_known_teb(const void* p) {
    if (!p) return false;
    for (const auto& slot : known_tebs) {
        auto v = slot.load(std::memory_order_acquire);
        if (!v) return false;
        if (v == p) return true;
    }
    return false;
}

}
