#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <set>
#include "host/loader/module_manager.h"
#include "host/runtime/host_entry.h"
#include "host/runtime/process_env.h"
#include "host/runtime/thunks.h"
#include "host/runtime/trace.h"

namespace ntmac::runtime {

namespace {

enum class UnimplementedMode { Abort, Return0, Strict };

struct TraceConfig {
    bool load_dll = false;
    bool unresolved = false;
    bool relay = false;
    std::set<std::string> relay_exclude;   // 小文字
    UnimplementedMode unimplemented = UnimplementedMode::Abort;
};

TraceConfig config;
const loader::ModuleManager* g_modules = nullptr;
std::mutex output_mutex;

// 生成したコードから参照される文字列や構造体．プロセスの終わりまで保持する
std::mutex storage_mutex;
std::deque<std::string> names;

struct RelayInfo {
    const char* name;
    void* target;
};
std::deque<RelayInfo> relay_infos;

struct StubInfo {
    const char* name;
    std::atomic<bool> reported{false};
};
std::deque<StubInfo> stub_infos;

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

std::vector<std::string> split(const char* s) {
    std::vector<std::string> out;
    if (!s) return out;
    std::string cur;
    for (const char* p = s;; p++) {
        if (*p == ',' || *p == 0) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
            if (!*p) break;
        } else if (!std::isspace(static_cast<unsigned char>(*p))) {
            cur += *p;
        }
    }
    return out;
}

/** "kernel32.dll" → "kernel32" */
std::string module_stem(std::string_view dll) {
    auto s = lower(dll);
    if (s.size() > 4 && s.ends_with(".dll")) s.resize(s.size() - 4);
    return s;
}

const char* store_name(std::string_view dll, std::string_view symbol) {
    std::lock_guard lock(storage_mutex);
    names.push_back(std::string(dll) + "." + std::string(symbol));
    return names.back().c_str();
}

uint64_t current_thread_id() {
    auto teb = current_teb();
    return teb ? teb->UniqueThread : 0;
}

/** 未実装の関数が呼ばれたとき (ms_abi，gs はホスト) */
__attribute__((ms_abi)) uint64_t on_unimplemented(StubInfo* info, void* return_address) {
    bool first = !info->reported.exchange(true);
    if (config.unimplemented == UnimplementedMode::Return0) {
        if (first) {
            trace_printf("[%04llx] 未実装: %s (呼び出し元: %s)．0 を返して続行します",
                         static_cast<unsigned long long>(current_thread_id()), info->name,
                         describe_address(return_address).c_str());
        }
        return 0;
    }

    trace_printf("[%04llx] 未実装の関数 %s が呼ばれたため終了します (呼び出し元: %s)",
                 static_cast<unsigned long long>(current_thread_id()), info->name,
                 describe_address(return_address).c_str());
    std::fflush(nullptr);
    std::_Exit(1);
}

}

}

// thunks.S の ntmac_relay_thunk から呼ばれる (ms_abi，gs はホスト)
extern "C" __attribute__((ms_abi)) void ntmac_relay_enter(ntmac::runtime::RelayInfo* info, const uint64_t* args) {
    using namespace ntmac::runtime;
    trace_printf("[%04llx] Call %s(%016llx, %016llx, %016llx, %016llx) ret=%s",
                 static_cast<unsigned long long>(current_thread_id()), info->name,
                 static_cast<unsigned long long>(args[0]), static_cast<unsigned long long>(args[1]),
                 static_cast<unsigned long long>(args[2]), static_cast<unsigned long long>(args[3]),
                 describe_address(reinterpret_cast<void*>(args[-1])).c_str());
}

extern "C" __attribute__((ms_abi)) void ntmac_relay_leave(ntmac::runtime::RelayInfo* info, uint64_t result) {
    using namespace ntmac::runtime;
    trace_printf("[%04llx] Ret  %s() retval=%016llx", static_cast<unsigned long long>(current_thread_id()),
                 info->name, static_cast<unsigned long long>(result));
}

namespace ntmac::runtime {

void init_trace(const loader::ModuleManager* modules) {
    g_modules = modules;

    for (const auto& item : split(std::getenv("NTMAC_TRACE"))) {
        auto name = lower(item);
        if (name == "loaddll") config.load_dll = true;
        else if (name == "unresolved") config.unresolved = true;
        else if (name == "relay") config.relay = true;
        else trace_printf("warning: NTMAC_TRACE の不明な項目: %s", item.c_str());
    }

    for (const auto& item : split(std::getenv("NTMAC_RELAY_EXCLUDE"))) {
        // "kernel32.dll.HeapAlloc" のような指定も "kernel32.heapalloc" として扱う
        auto s = lower(item);
        if (auto pos = s.find(".dll"); pos != std::string::npos) s.erase(pos, 4);
        config.relay_exclude.insert(s);
    }

    if (auto mode = std::getenv("NTMAC_UNIMPLEMENTED")) {
        std::string m = lower(mode);
        if (m == "abort") config.unimplemented = UnimplementedMode::Abort;
        else if (m == "return0") config.unimplemented = UnimplementedMode::Return0;
        else if (m == "strict") config.unimplemented = UnimplementedMode::Strict;
        else trace_printf("warning: NTMAC_UNIMPLEMENTED の不明な値: %s", mode);
    }
}

bool trace_enabled(TraceChannel channel) {
    switch (channel) {
    case TraceChannel::LoadDll:    return config.load_dll;
    case TraceChannel::Unresolved: return config.unresolved;
    case TraceChannel::Relay:      return config.relay;
    }
    return false;
}

void trace_printf(const char* format, ...) {
    char buf[1024];
    va_list args;
    va_start(args, format);
    int n = std::vsnprintf(buf, sizeof(buf) - 1, format, args);
    va_end(args);
    if (n < 0) return;
    size_t len = std::min<size_t>(static_cast<size_t>(n), sizeof(buf) - 2);
    buf[len++] = '\n';

    std::lock_guard lock(output_mutex);
    ssize_t unused = write(STDERR_FILENO, buf, len);
    (void)unused;
}

std::string describe_address(const void* p) {
    char buf[512];
    if (auto m = g_modules ? g_modules->find_by_address(p) : nullptr) {
        std::snprintf(buf, sizeof(buf), "%s+0x%tx", m->name.c_str(), static_cast<const uint8_t*>(p) - m->image->base());
    } else {
        std::snprintf(buf, sizeof(buf), "%p", p);
    }
    return buf;
}

void* make_unimplemented_stub(std::string_view dll, std::string_view symbol) {
    if (config.unimplemented == UnimplementedMode::Strict) return nullptr;

    const char* name = store_name(module_stem(dll), symbol);
    if (config.unresolved) trace_printf("unresolved: %s をスタブにします", name);

    StubInfo* info;
    {
        std::lock_guard lock(storage_mutex);
        info = &stub_infos.emplace_back();
        info->name = name;
    }
    return make_bound_host_entry(reinterpret_cast<void*>(&on_unimplemented), info);
}

void* wrap_relay(std::string_view dll, std::string_view symbol, void* target) {
    if (!config.relay || !target) return target;

    auto stem = module_stem(dll);
    if (config.relay_exclude.contains(stem) || config.relay_exclude.contains(stem + "." + lower(symbol))) {
        return target;
    }

    RelayInfo* info;
    {
        const char* name = store_name(stem, symbol);
        std::lock_guard lock(storage_mutex);
        info = &relay_infos.emplace_back(RelayInfo{name, target});
    }
    return make_jump_entry(reinterpret_cast<const void*>(&ntmac_relay_thunk), info);
}

}
