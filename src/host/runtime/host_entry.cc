#include <sys/mman.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>
#include "host/runtime/host_entry.h"
#include "host/runtime/thunks.h"
#include "host/loader/exceptions/load_error.h"
#include "purefuncs.h"

namespace ntmac::runtime {

namespace {

// movabs rax, <impl>
// jmp    qword ptr [rip + 0]
// .quad  <ntmac_host_trampoline>
constexpr size_t kStubSize = 32;

void write_stub(uint8_t* p, void* impl) {
    auto trampoline = reinterpret_cast<uint64_t>(&ntmac_host_trampoline);
    auto target = reinterpret_cast<uint64_t>(impl);

    std::memset(p, 0xCC, kStubSize); // int3
    p[0] = 0x48; p[1] = 0xB8;
    std::memcpy(p + 2, &target, sizeof(target));
    p[10] = 0xFF; p[11] = 0x25;
    std::memset(p + 12, 0, 4);
    std::memcpy(p + 16, &trampoline, sizeof(trampoline));
}

}

std::vector<void*> make_host_entries(std::span<void* const> impls) {
    std::vector<void*> entries;
    if (impls.empty()) return entries;

    // 全スタブを1つの領域にまとめて書き込み，以後は実行専用にする
    size_t size = align_up(impls.size() * kStubSize, getpagesize());
    void* mem = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mem == MAP_FAILED) {
        throw loader::LoadError("ホスト関数の入口スタブ用のメモリを確保できませんでした");
    }

    auto base = static_cast<uint8_t*>(mem);
    for (size_t i = 0; i < impls.size(); i++) {
        write_stub(base + i * kStubSize, impls[i]);
        entries.push_back(base + i * kStubSize);
    }

    if (mprotect(mem, size, PROT_READ | PROT_EXEC) != 0) {
        throw loader::LoadError("ホスト関数の入口スタブを実行可能にできませんでした");
    }
    return entries;
}

}
