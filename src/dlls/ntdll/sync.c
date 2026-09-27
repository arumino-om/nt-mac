#include "ntdll/ntdll.h"
#include "shared/ntmachost.h"

/*
 * 同期の基本部品．待機と起床はすべて RtlWaitOnAddress / RtlWakeAddress* (ホストの os_sync_wait_on_address) で行う．
 */

/* ---- WaitOnAddress ---- */

static int64_t timeout_100ns(const LARGE_INTEGER* timeout) {
    if (!timeout) return -1;                        /* 無期限 */
    if (timeout->QuadPart <= 0) return -timeout->QuadPart; /* 負は相対時間 (0 はポーリング) */

    /* 正は絶対時刻 */
    LARGE_INTEGER now;
    NtQuerySystemTime(&now);
    return timeout->QuadPart > now.QuadPart ? timeout->QuadPart - now.QuadPart : 0;
}

NTSTATUS NTAPI RtlWaitOnAddress(const volatile void* Address, PVOID CompareAddress, SIZE_T Size, PLARGE_INTEGER Timeout) {
    ntmachost_wait_args args = {.Address = Address, .Size = (uint32_t)Size, .Timeout = timeout_100ns(Timeout)};
    switch (Size) {
    case 1: args.CompareValue = *(const uint8_t*)CompareAddress; break;
    case 2: args.CompareValue = *(const uint16_t*)CompareAddress; break;
    case 4: args.CompareValue = *(const uint32_t*)CompareAddress; break;
    case 8: args.CompareValue = *(const uint64_t*)CompareAddress; break;
    default: return NTMAC_STATUS_INVALID_PARAMETER;
    }
    return ntmachost_call(NTMACHOST_WAIT_ON_ADDRESS, &args);
}

VOID NTAPI RtlWakeAddressSingle(PVOID Address) {
    ntmachost_wake_args args = {.Address = Address, .WakeAll = 0};
    ntmachost_call(NTMACHOST_WAKE_BY_ADDRESS, &args);
}

VOID NTAPI RtlWakeAddressAll(PVOID Address) {
    ntmachost_wake_args args = {.Address = Address, .WakeAll = 1};
    ntmachost_call(NTMACHOST_WAKE_BY_ADDRESS, &args);
}

static NTSTATUS wait_while_equal64(volatile uint64_t* p, uint64_t value, const LARGE_INTEGER* timeout) {
    return RtlWaitOnAddress(p, &value, sizeof(value), (PLARGE_INTEGER)timeout);
}

/* ---- SRW ロック ----
 * bit 0: 排他で保持中，bit 1-62: 共有で保持しているスレッド数，bit 63: 待っているスレッドがいる */

#define SRW_EXCLUSIVE   1ULL
#define SRW_SHARED_ONE  2ULL
#define SRW_WAITERS     (1ULL << 63)

static volatile uint64_t* srw_value(PRTL_SRWLOCK lock) {
    return (volatile uint64_t*)&lock->Ptr;
}

/* 待機者ありの印を付けてから待つ．印を付けられなかった (値が変わった) 場合はすぐ戻る */
static void srw_wait(volatile uint64_t* v, uint64_t seen) {
    if (!(seen & SRW_WAITERS)) {
        if (!__atomic_compare_exchange_n(v, &seen, seen | SRW_WAITERS, FALSE, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) return;
        seen |= SRW_WAITERS;
    }
    wait_while_equal64(v, seen, NULL);
}

VOID NTAPI RtlInitializeSRWLock(PRTL_SRWLOCK Lock) {
    Lock->Ptr = NULL;
}

BOOLEAN NTAPI RtlTryAcquireSRWLockExclusive(PRTL_SRWLOCK Lock) {
    volatile uint64_t* v = srw_value(Lock);
    uint64_t s = __atomic_load_n(v, __ATOMIC_RELAXED);
    return (s & ~SRW_WAITERS) == 0 &&
           __atomic_compare_exchange_n(v, &s, s | SRW_EXCLUSIVE, FALSE, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

BOOLEAN NTAPI RtlTryAcquireSRWLockShared(PRTL_SRWLOCK Lock) {
    volatile uint64_t* v = srw_value(Lock);
    uint64_t s = __atomic_load_n(v, __ATOMIC_RELAXED);
    return !(s & SRW_EXCLUSIVE) &&
           __atomic_compare_exchange_n(v, &s, s + SRW_SHARED_ONE, FALSE, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

VOID NTAPI RtlAcquireSRWLockExclusive(PRTL_SRWLOCK Lock) {
    volatile uint64_t* v = srw_value(Lock);
    for (;;) {
        uint64_t s = __atomic_load_n(v, __ATOMIC_RELAXED);
        if ((s & ~SRW_WAITERS) == 0) {
            if (__atomic_compare_exchange_n(v, &s, s | SRW_EXCLUSIVE, FALSE, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return;
            continue;
        }
        srw_wait(v, s);
    }
}

VOID NTAPI RtlAcquireSRWLockShared(PRTL_SRWLOCK Lock) {
    volatile uint64_t* v = srw_value(Lock);
    for (;;) {
        uint64_t s = __atomic_load_n(v, __ATOMIC_RELAXED);
        if (!(s & SRW_EXCLUSIVE)) {
            if (__atomic_compare_exchange_n(v, &s, s + SRW_SHARED_ONE, FALSE, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return;
            continue;
        }
        srw_wait(v, s);
    }
}

VOID NTAPI RtlReleaseSRWLockExclusive(PRTL_SRWLOCK Lock) {
    volatile uint64_t* v = srw_value(Lock);
    uint64_t old = __atomic_exchange_n(v, 0, __ATOMIC_RELEASE);
    if (old & SRW_WAITERS) RtlWakeAddressAll((PVOID)v);
}

VOID NTAPI RtlReleaseSRWLockShared(PRTL_SRWLOCK Lock) {
    volatile uint64_t* v = srw_value(Lock);
    uint64_t s = __atomic_sub_fetch(v, SRW_SHARED_ONE, __ATOMIC_RELEASE);
    /* 最後の共有保持者が抜けたら，待機者を起こす */
    if (s == SRW_WAITERS && __atomic_compare_exchange_n(v, &s, 0, FALSE, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
        RtlWakeAddressAll((PVOID)v);
    }
}

/* ---- 条件変数 (値は通知のたびに増える世代番号) ---- */

static volatile uint64_t* cond_value(PRTL_CONDITION_VARIABLE cond) {
    return (volatile uint64_t*)&cond->Ptr;
}

VOID NTAPI RtlInitializeConditionVariable(PRTL_CONDITION_VARIABLE Cond) {
    Cond->Ptr = NULL;
}

NTSTATUS NTAPI RtlSleepConditionVariableSRW(PRTL_CONDITION_VARIABLE Cond, PRTL_SRWLOCK Lock,
                                            const LARGE_INTEGER* Timeout, ULONG Flags) {
    volatile uint64_t* v = cond_value(Cond);
    uint64_t seq = __atomic_load_n(v, __ATOMIC_ACQUIRE);
    BOOL shared = Flags & CONDITION_VARIABLE_LOCKMODE_SHARED;

    if (shared) RtlReleaseSRWLockShared(Lock);
    else RtlReleaseSRWLockExclusive(Lock);

    NTSTATUS status = wait_while_equal64(v, seq, Timeout);

    if (shared) RtlAcquireSRWLockShared(Lock);
    else RtlAcquireSRWLockExclusive(Lock);
    return status == NTMAC_STATUS_TIMEOUT ? NTMAC_STATUS_TIMEOUT : NTMAC_STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlSleepConditionVariableCS(PRTL_CONDITION_VARIABLE Cond, PRTL_CRITICAL_SECTION Cs,
                                           const LARGE_INTEGER* Timeout) {
    volatile uint64_t* v = cond_value(Cond);
    uint64_t seq = __atomic_load_n(v, __ATOMIC_ACQUIRE);

    /* 再帰的に入っている場合も，すべて抜けてから待ち，同じ回数だけ入り直す */
    LONG recursion = Cs->RecursionCount;
    for (LONG i = 0; i < recursion; i++) RtlLeaveCriticalSection(Cs);
    NTSTATUS status = wait_while_equal64(v, seq, Timeout);
    for (LONG i = 0; i < recursion; i++) RtlEnterCriticalSection(Cs);
    return status == NTMAC_STATUS_TIMEOUT ? NTMAC_STATUS_TIMEOUT : NTMAC_STATUS_SUCCESS;
}

VOID NTAPI RtlWakeConditionVariable(PRTL_CONDITION_VARIABLE Cond) {
    __atomic_add_fetch(cond_value(Cond), 1, __ATOMIC_RELEASE);
    RtlWakeAddressSingle((PVOID)cond_value(Cond));
}

VOID NTAPI RtlWakeAllConditionVariable(PRTL_CONDITION_VARIABLE Cond) {
    __atomic_add_fetch(cond_value(Cond), 1, __ATOMIC_RELEASE);
    RtlWakeAddressAll((PVOID)cond_value(Cond));
}

/* ---- RunOnce ----
 * 下位2ビット: 0 = 未実行，1 = 実行中，2 = 完了．完了時の上位ビットは Context */

#define ONCE_STATE_MASK  3ULL
#define ONCE_RUNNING     1ULL
#define ONCE_DONE        2ULL

VOID NTAPI RtlRunOnceInitialize(PRTL_RUN_ONCE Once) {
    Once->Ptr = NULL;
}

NTSTATUS NTAPI RtlRunOnceExecuteOnce(PRTL_RUN_ONCE Once, PRTL_RUN_ONCE_INIT_FN Func, PVOID Param, PVOID* Context) {
    volatile uint64_t* v = (volatile uint64_t*)&Once->Ptr;
    for (;;) {
        uint64_t s = __atomic_load_n(v, __ATOMIC_ACQUIRE);
        if ((s & ONCE_STATE_MASK) == ONCE_DONE) {
            if (Context) *Context = (PVOID)(s & ~ONCE_STATE_MASK);
            return NTMAC_STATUS_SUCCESS;
        }
        if (s == 0) {
            if (!__atomic_compare_exchange_n(v, &s, ONCE_RUNNING, FALSE, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) continue;

            PVOID ctx = NULL;
            BOOL ok = Func(Once, Param, &ctx) != 0;
            __atomic_store_n(v, ok ? (((uint64_t)ctx & ~ONCE_STATE_MASK) | ONCE_DONE) : 0, __ATOMIC_RELEASE);
            RtlWakeAddressAll((PVOID)v);
            if (!ok) return NTMAC_STATUS_UNSUCCESSFUL;
            if (Context) *Context = ctx;
            return NTMAC_STATUS_SUCCESS;
        }
        wait_while_equal64(v, s, NULL); /* 他のスレッドが実行中 */
    }
}

/* ---- SList (ロックフリーのスタック) ----
 * SLIST_HEADER: 前半 64 ビット = Depth (16) | Sequence (48)，後半 64 ビット = 先頭の要素 (16 バイト境界) */

typedef unsigned __int128 u128;

static u128 slist_load(PSLIST_HEADER head) {
    return ((u128)head->Region << 64) | head->Alignment;  /* 非アトミックに読み，CAS で確かめる */
}

static u128 slist_make(uint64_t depth, uint64_t seq, PSLIST_ENTRY first) {
    return ((u128)(ULONG_PTR)first << 64) | ((seq & 0xFFFFFFFFFFFFULL) << 16) | (depth & 0xFFFF);
}

static BOOL slist_cas(PSLIST_HEADER head, u128 expected, u128 desired) {
    return __sync_bool_compare_and_swap((volatile u128*)head, expected, desired);
}

VOID NTAPI RtlInitializeSListHead(PSLIST_HEADER ListHead) {
    ListHead->Alignment = 0;
    ListHead->Region = 0;
}

PSLIST_ENTRY NTAPI RtlFirstEntrySList(const SLIST_HEADER* ListHead) {
    return (PSLIST_ENTRY)(ULONG_PTR)(ListHead->Region & ~0xFULL);
}

WORD NTAPI RtlQueryDepthSList(PSLIST_HEADER ListHead) {
    return (WORD)(ListHead->Alignment & 0xFFFF);
}

PSLIST_ENTRY NTAPI RtlInterlockedPushEntrySList(PSLIST_HEADER ListHead, PSLIST_ENTRY ListEntry) {
    for (;;) {
        u128 old = slist_load(ListHead);
        PSLIST_ENTRY first = (PSLIST_ENTRY)(ULONG_PTR)((uint64_t)(old >> 64) & ~0xFULL);
        uint64_t lo = (uint64_t)old;
        ListEntry->Next = first;
        if (slist_cas(ListHead, old, slist_make((lo & 0xFFFF) + 1, (lo >> 16) + 1, ListEntry))) return first;
    }
}

PSLIST_ENTRY NTAPI RtlInterlockedPopEntrySList(PSLIST_HEADER ListHead) {
    for (;;) {
        u128 old = slist_load(ListHead);
        PSLIST_ENTRY first = (PSLIST_ENTRY)(ULONG_PTR)((uint64_t)(old >> 64) & ~0xFULL);
        if (!first) return NULL;
        uint64_t lo = (uint64_t)old;
        if (slist_cas(ListHead, old, slist_make((lo & 0xFFFF) - 1, (lo >> 16) + 1, first->Next))) return first;
    }
}

PSLIST_ENTRY NTAPI RtlInterlockedFlushSList(PSLIST_HEADER ListHead) {
    for (;;) {
        u128 old = slist_load(ListHead);
        PSLIST_ENTRY first = (PSLIST_ENTRY)(ULONG_PTR)((uint64_t)(old >> 64) & ~0xFULL);
        if (!first) return NULL;
        uint64_t lo = (uint64_t)old;
        if (slist_cas(ListHead, old, slist_make(0, (lo >> 16) + 1, NULL))) return first;
    }
}
