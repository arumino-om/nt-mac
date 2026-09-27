#include <os/os_sync_wait_on_address.h>
#include <cerrno>
#include <cstring>
#include "host/ntmachost/services.h"

// WaitOnAddress / WakeByAddress を os_sync_wait_on_address (macOS 14.4 以降) で実装する．
// os_sync は 4 / 8 バイトの値しか扱えないので，1 / 2 バイトの値は，それを含む 4 バイト境界の値で待つ
// (他のバイトへの書き込みでも起きることがあるが，呼び出し側は値を確かめ直すので問題ない)．

namespace ntmac::ntmachost {

namespace {

struct WaitTarget {
    void* address;      // 実際に待つアドレス (4 か 8 バイト境界)
    size_t size;        // 4 か 8
};

WaitTarget wait_target(const volatile void* address, uint32_t size) {
    auto p = reinterpret_cast<uintptr_t>(address);
    if (size == 8) return {reinterpret_cast<void*>(p), 8};
    return {reinterpret_cast<void*>(p & ~uintptr_t{3}), 4};
}

uint64_t load_value(const volatile void* address, uint32_t size) {
    switch (size) {
    case 1: return *static_cast<const volatile uint8_t*>(address);
    case 2: return *static_cast<const volatile uint16_t*>(address);
    case 4: return *static_cast<const volatile uint32_t*>(address);
    default: return *static_cast<const volatile uint64_t*>(address);
    }
}

}

ntmac_ntstatus wait_on_address(const ntmachost_wait_args* args) {
    if (args->Size != 1 && args->Size != 2 && args->Size != 4 && args->Size != 8) {
        return NTMAC_STATUS_INVALID_PARAMETER;
    }

    auto target = wait_target(args->Address, args->Size);
    uint64_t current = target.size == 8 ? *static_cast<volatile uint64_t*>(target.address)
                                        : *static_cast<volatile uint32_t*>(target.address);
    // 待つ前に値を確かめる (既に変わっていればすぐ戻る)
    if (load_value(args->Address, args->Size) != (args->CompareValue & (args->Size == 8 ? ~0ULL : (1ULL << (args->Size * 8)) - 1))) {
        return NTMAC_STATUS_SUCCESS;
    }

    int ret;
    if (args->Timeout < 0) {
        ret = os_sync_wait_on_address(target.address, current, target.size, OS_SYNC_WAIT_ON_ADDRESS_NONE);
    } else {
        uint64_t ns = static_cast<uint64_t>(args->Timeout) * 100;
        if (ns == 0) return NTMAC_STATUS_TIMEOUT;
        ret = os_sync_wait_on_address_with_timeout(target.address, current, target.size,
                                                   OS_SYNC_WAIT_ON_ADDRESS_NONE, OS_CLOCK_MACH_ABSOLUTE_TIME, ns);
    }
    if (ret < 0 && errno == ETIMEDOUT) return NTMAC_STATUS_TIMEOUT;
    return NTMAC_STATUS_SUCCESS; // 起こされた・値が違った・割り込み．いずれも呼び出し側が値を確かめ直す
}

ntmac_ntstatus wake_by_address(const ntmachost_wake_args* args) {
    // どのサイズで待っているか分からないので，4 バイト境界と 8 バイト境界の両方を起こす
    auto p = reinterpret_cast<uintptr_t>(args->Address);
    void* addr4 = reinterpret_cast<void*>(p & ~uintptr_t{3});
    void* addr8 = reinterpret_cast<void*>(p);
    if (args->WakeAll) {
        os_sync_wake_by_address_all(addr4, 4, OS_SYNC_WAKE_BY_ADDRESS_NONE);
        if ((p & 7) == 0) os_sync_wake_by_address_all(addr8, 8, OS_SYNC_WAKE_BY_ADDRESS_NONE);
    } else {
        if (os_sync_wake_by_address_any(addr4, 4, OS_SYNC_WAKE_BY_ADDRESS_NONE) != 0 && (p & 7) == 0) {
            os_sync_wake_by_address_any(addr8, 8, OS_SYNC_WAKE_BY_ADDRESS_NONE);
        }
    }
    return NTMAC_STATUS_SUCCESS;
}

}
