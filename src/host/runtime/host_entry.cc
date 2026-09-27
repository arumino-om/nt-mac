#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <sys/mman.h>
#include <cstdint>
#include <cstring>
#include <mutex>
#include "host/loader/exceptions/load_error.h"
#include "host/runtime/host_entry.h"
#include "host/runtime/thunks.h"
#include "purefuncs.h"

namespace ntmac::runtime {

namespace {

/**
 * 実行時に生成するコードの置き場．
 *
 * 同じ物理ページを「書き込み用 (RW)」と「実行用 (RX)」の2つのアドレスに割り当てる．
 * 実行可能なページを書き換えずに済むので，他のスレッドが既存のコードを実行中でも安全に追記できる．
 * 一度書いたコードは書き換えない (Rosetta の翻訳済みのコードと食い違わないようにするため)．
 */
class CodeArena {
public:
    uint8_t* write(const uint8_t* code, size_t size) {
        std::lock_guard lock(mutex_);
        size_t slot = align_up(size, 16);
        if (used_ + slot > kChunkSize || !exec_) new_chunk();
        std::memcpy(write_ + used_, code, size);
        uint8_t* p = exec_ + used_;
        used_ += slot;
        return p;
    }

private:
    static constexpr size_t kChunkSize = 64 * 1024;

    void new_chunk() {
        mach_vm_address_t rw = 0;
        if (mach_vm_allocate(mach_task_self(), &rw, kChunkSize, VM_FLAGS_ANYWHERE) != KERN_SUCCESS) {
            throw loader::LoadError("コード生成用のメモリを確保できませんでした");
        }
        std::memset(reinterpret_cast<void*>(rw), 0xCC, kChunkSize); // int3

        mach_vm_address_t rx = 0;
        vm_prot_t cur = 0, max = 0;
        if (mach_vm_remap(mach_task_self(), &rx, kChunkSize, 0, VM_FLAGS_ANYWHERE, mach_task_self(), rw, FALSE,
                          &cur, &max, VM_INHERIT_NONE) != KERN_SUCCESS ||
            mprotect(reinterpret_cast<void*>(rx), kChunkSize, PROT_READ | PROT_EXEC) != 0) {
            throw loader::LoadError("コード生成用のメモリを実行可能にできませんでした");
        }

        write_ = reinterpret_cast<uint8_t*>(rw);
        exec_ = reinterpret_cast<uint8_t*>(rx);
        used_ = 0;
    }

    std::mutex mutex_;
    uint8_t* write_ = nullptr;
    uint8_t* exec_ = nullptr;
    size_t used_ = 0;
};

CodeArena& arena() {
    static CodeArena a;
    return a;
}

/** x86-64 の機械語を組み立てる */
class Code {
public:
    Code& bytes(std::initializer_list<uint8_t> b) {
        for (auto x : b) buf_[len_++] = x;
        return *this;
    }
    Code& imm64(uint64_t v) {
        std::memcpy(buf_ + len_, &v, sizeof(v));
        len_ += sizeof(v);
        return *this;
    }
    Code& imm64(const void* p) { return imm64(reinterpret_cast<uint64_t>(p)); }

    Code& movabs_rax(const void* p) { return bytes({0x48, 0xB8}).imm64(p); }
    Code& movabs_rcx(const void* p) { return bytes({0x48, 0xB9}).imm64(p); }
    Code& mov_rdx_from_rsp() { return bytes({0x48, 0x8B, 0x14, 0x24}); }            // mov rdx, [rsp]
    Code& jmp_abs(const void* p) { return bytes({0xFF, 0x25, 0, 0, 0, 0}).imm64(p); } // jmp [rip+0]

    void* emit() { return arena().write(buf_, len_); }

private:
    uint8_t buf_[64];
    size_t len_ = 0;
};

const void* trampoline() {
    return reinterpret_cast<const void*>(&ntmac_host_trampoline);
}

}

void* make_host_entry(void* impl) {
    return Code().movabs_rax(impl).jmp_abs(trampoline()).emit();
}

std::vector<void*> make_host_entries(std::span<void* const> impls) {
    std::vector<void*> entries;
    for (auto impl : impls) entries.push_back(make_host_entry(impl));
    return entries;
}

void* make_bound_host_entry(void* impl, const void* ctx) {
    return Code().movabs_rcx(ctx).mov_rdx_from_rsp().movabs_rax(impl).jmp_abs(trampoline()).emit();
}

void* make_jump_entry(const void* target, const void* ctx) {
    return Code().movabs_rax(ctx).jmp_abs(target).emit();
}

}
