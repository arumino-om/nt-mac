#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <sys/mman.h>
#include <unistd.h>
#include <map>
#include <mutex>
#include <optional>
#include "host/ntmachost/services.h"
#include "purefuncs.h"

// Windows の仮想メモリを mmap で再現する．
//   予約 (MEM_RESERVE)       → PROT_NONE でマッピング
//   コミット (MEM_COMMIT)    → mprotect で保護を設定 (初回アクセス時にゼロページになる)
//   デコミット (MEM_DECOMMIT) → PROT_NONE で同じ範囲をマッピングし直して中身を捨てる
//   解放 (MEM_RELEASE)       → 予約した範囲全体を munmap

namespace ntmac::ntmachost {

namespace {

// NtAllocateVirtualMemory で予約した範囲 (先頭 → サイズ)．MEM_RELEASE は先頭だけで範囲全体を解放するため
std::mutex regions_mutex;
std::map<uint64_t, uint64_t> regions;

uint64_t page_size() {
    static const uint64_t size = static_cast<uint64_t>(getpagesize());
    return size;
}

std::optional<int> to_posix_protection(uint32_t protect) {
    // PAGE_GUARD / PAGE_NOCACHE / PAGE_WRITECOMBINE は無視する
    switch (protect & 0xFF) {
    case NTMAC_PAGE_NOACCESS:          return PROT_NONE;
    case NTMAC_PAGE_READONLY:          return PROT_READ;
    case NTMAC_PAGE_READWRITE:
    case NTMAC_PAGE_WRITECOPY:         return PROT_READ | PROT_WRITE;
    case NTMAC_PAGE_EXECUTE:
    case NTMAC_PAGE_EXECUTE_READ:      return PROT_READ | PROT_EXEC;
    case NTMAC_PAGE_EXECUTE_READWRITE:
    case NTMAC_PAGE_EXECUTE_WRITECOPY: return PROT_READ | PROT_WRITE | PROT_EXEC;
    default:                           return std::nullopt;
    }
}

uint32_t to_windows_protection(vm_prot_t prot) {
    bool r = prot & VM_PROT_READ, w = prot & VM_PROT_WRITE, x = prot & VM_PROT_EXECUTE;
    if (x) return w ? NTMAC_PAGE_EXECUTE_READWRITE : (r ? NTMAC_PAGE_EXECUTE_READ : NTMAC_PAGE_EXECUTE);
    if (w) return NTMAC_PAGE_READWRITE;
    if (r) return NTMAC_PAGE_READONLY;
    return NTMAC_PAGE_NOACCESS;
}

/**
 * addr を含む予約済みの範囲を返します．regions_mutex を取得した状態で呼ぶこと．
 */
std::optional<std::pair<uint64_t, uint64_t>> find_region(uint64_t addr) {
    auto it = regions.upper_bound(addr);
    if (it == regions.begin()) return std::nullopt;
    --it;
    if (addr - it->first >= it->second) return std::nullopt;
    return *it;
}

}

ntmac_ntstatus allocate_virtual_memory(ntmachost_vm_args* args) {
    if (args->RegionSize == 0) return NTMAC_STATUS_INVALID_PARAMETER;
    if (!(args->Type & (NTMAC_MEM_COMMIT | NTMAC_MEM_RESERVE))) return NTMAC_STATUS_INVALID_PARAMETER;

    auto prot = to_posix_protection(args->Protect);
    if (!prot) return NTMAC_STATUS_INVALID_PAGE_PROTECTION;

    uint64_t begin = args->BaseAddress / page_size() * page_size();
    uint64_t end = align_up(args->BaseAddress + args->RegionSize, page_size());
    std::lock_guard lock(regions_mutex);

    if (args->Type & NTMAC_MEM_RESERVE) {
        // 新しく予約する．アドレスの指定があればその位置に置けなければ失敗 (MAP_FIXED は既存のマッピングを壊すので使わない)
        int initial = (args->Type & NTMAC_MEM_COMMIT) ? *prot : PROT_NONE;
        void* hint = reinterpret_cast<void*>(begin);
        void* p = mmap(hint, end - begin, initial, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p == MAP_FAILED) return NTMAC_STATUS_NO_MEMORY;
        if (begin != 0 && p != hint) {
            munmap(p, end - begin);
            return NTMAC_STATUS_CONFLICTING_ADDRESSES;
        }
        begin = reinterpret_cast<uint64_t>(p);
        regions[begin] = end - begin;
    } else {
        // 予約済みの範囲をコミットする
        auto region = find_region(begin);
        if (!region || end - region->first > region->second) return NTMAC_STATUS_MEMORY_NOT_ALLOCATED;
        if (mprotect(reinterpret_cast<void*>(begin), end - begin, *prot) != 0) return NTMAC_STATUS_INVALID_PARAMETER;
    }

    args->BaseAddress = begin;
    args->RegionSize = end - begin;
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus free_virtual_memory(ntmachost_vm_args* args) {
    std::lock_guard lock(regions_mutex);

    if (args->Type == NTMAC_MEM_RELEASE) {
        auto it = regions.find(args->BaseAddress);
        if (it == regions.end() || args->RegionSize != 0) return NTMAC_STATUS_MEMORY_NOT_ALLOCATED;
        munmap(reinterpret_cast<void*>(it->first), it->second);
        args->RegionSize = it->second;
        regions.erase(it);
        return NTMAC_STATUS_SUCCESS;
    }

    if (args->Type == NTMAC_MEM_DECOMMIT) {
        uint64_t begin = args->BaseAddress / page_size() * page_size();
        auto region = find_region(begin);
        if (!region) return NTMAC_STATUS_MEMORY_NOT_ALLOCATED;

        // RegionSize が 0 なら予約した範囲の終わりまで
        uint64_t region_end = region->first + region->second;
        uint64_t end = args->RegionSize == 0 ? region_end : align_up(args->BaseAddress + args->RegionSize, page_size());
        if (end > region_end) return NTMAC_STATUS_MEMORY_NOT_ALLOCATED;

        // 自分で予約した範囲なので MAP_FIXED で置き換えてよい
        void* p = mmap(reinterpret_cast<void*>(begin), end - begin, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
        if (p == MAP_FAILED) return NTMAC_STATUS_UNSUCCESSFUL;
        args->BaseAddress = begin;
        args->RegionSize = end - begin;
        return NTMAC_STATUS_SUCCESS;
    }

    return NTMAC_STATUS_INVALID_PARAMETER;
}

ntmac_ntstatus protect_virtual_memory(ntmachost_vm_args* args) {
    auto prot = to_posix_protection(args->Protect);
    if (!prot) return NTMAC_STATUS_INVALID_PAGE_PROTECTION;

    uint64_t begin = args->BaseAddress / page_size() * page_size();
    uint64_t end = align_up(args->BaseAddress + args->RegionSize, page_size());
    if (end == begin) return NTMAC_STATUS_INVALID_PARAMETER;

    ntmachost_vm_query_args query{.Address = begin};
    auto status = query_virtual_memory(&query);
    if (status != NTMAC_STATUS_SUCCESS) return status;
    if (query.State == NTMAC_MEM_FREE) return NTMAC_STATUS_MEMORY_NOT_ALLOCATED;

    if (mprotect(reinterpret_cast<void*>(begin), end - begin, *prot) != 0) return NTMAC_STATUS_INVALID_PARAMETER;
    args->OldProtect = query.Protect;
    args->BaseAddress = begin;
    args->RegionSize = end - begin;
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus query_virtual_memory(ntmachost_vm_query_args* args) {
    uint64_t page = args->Address / page_size() * page_size();

    mach_vm_address_t addr = page;
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info{};
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    kern_return_t kr = mach_vm_region(mach_task_self(), &addr, &size, VM_REGION_BASIC_INFO_64,
                                      reinterpret_cast<vm_region_info_t>(&info), &count, &object);

    args->BaseAddress = page;
    if (kr != KERN_SUCCESS || addr > page) {
        // page はどの領域にも含まれない．次の領域の手前 (無ければユーザ空間の終わり) までが空き
        uint64_t next = kr == KERN_SUCCESS ? addr : 0x00007FFFFFFF0000ULL;
        args->AllocationBase = 0;
        args->RegionSize = next > page ? next - page : page_size();
        args->AllocationProtect = 0;
        args->State = NTMAC_MEM_FREE;
        args->Protect = NTMAC_PAGE_NOACCESS;
        args->Type = 0;
        return NTMAC_STATUS_SUCCESS;
    }

    uint64_t allocation_base = addr;
    {
        std::lock_guard lock(regions_mutex);
        if (auto region = find_region(page)) allocation_base = region->first;
    }

    args->AllocationBase = allocation_base;
    args->RegionSize = addr + size - page;
    args->Protect = to_windows_protection(info.protection);
    args->AllocationProtect = to_windows_protection(info.max_protection & (VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE));
    args->State = info.protection == VM_PROT_NONE ? NTMAC_MEM_RESERVE : NTMAC_MEM_COMMIT;
    args->Type = NTMAC_MEM_PRIVATE;
    return NTMAC_STATUS_SUCCESS;
}

}
