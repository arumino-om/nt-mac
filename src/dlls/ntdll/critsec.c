#include "ntdll/ntdll.h"
#include "shared/ntstatus.h"

/*
 * クリティカルセクション．
 *   OwningThread   保持しているスレッドの ID (空きは NULL)．待機は OwningThread の値の変化を RtlWaitOnAddress で待つ
 *   LockSemaphore  待機中のスレッド数 (Windows ではイベントのハンドルだが，ここでは数として使う)
 *   LockCount      Windows と同様に，空いていれば -1，保持されていれば 0 以上
 */

static HANDLE current_thread_id(void) {
    return (HANDLE)(ULONG_PTR)ntmac_current_teb()->UniqueThread;
}

static volatile LONG_PTR* waiter_count(PRTL_CRITICAL_SECTION cs) {
    return (volatile LONG_PTR*)&cs->LockSemaphore;
}

NTSTATUS NTAPI RtlInitializeCriticalSectionEx(PRTL_CRITICAL_SECTION CriticalSection, ULONG SpinCount, ULONG Flags) {
    (void)Flags;
    CriticalSection->DebugInfo = NULL;
    CriticalSection->LockCount = -1;
    CriticalSection->RecursionCount = 0;
    CriticalSection->OwningThread = NULL;
    CriticalSection->LockSemaphore = NULL;
    CriticalSection->SpinCount = SpinCount;
    return NTMAC_STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlInitializeCriticalSectionAndSpinCount(PRTL_CRITICAL_SECTION CriticalSection, ULONG SpinCount) {
    return RtlInitializeCriticalSectionEx(CriticalSection, SpinCount, 0);
}

NTSTATUS NTAPI RtlInitializeCriticalSection(PRTL_CRITICAL_SECTION CriticalSection) {
    return RtlInitializeCriticalSectionEx(CriticalSection, 0, 0);
}

NTSTATUS NTAPI RtlDeleteCriticalSection(PRTL_CRITICAL_SECTION CriticalSection) {
    (void)CriticalSection;
    return NTMAC_STATUS_SUCCESS;
}

BOOLEAN NTAPI RtlTryEnterCriticalSection(PRTL_CRITICAL_SECTION CriticalSection) {
    HANDLE self = current_thread_id();
    if (CriticalSection->OwningThread == self) {
        CriticalSection->RecursionCount++;
        return TRUE;
    }

    HANDLE expected = NULL;
    if (!__atomic_compare_exchange_n(&CriticalSection->OwningThread, &expected, self, FALSE,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        return FALSE;
    }
    CriticalSection->RecursionCount = 1;
    __atomic_add_fetch(&CriticalSection->LockCount, 1, __ATOMIC_RELAXED);
    return TRUE;
}

NTSTATUS NTAPI RtlEnterCriticalSection(PRTL_CRITICAL_SECTION CriticalSection) {
    while (!RtlTryEnterCriticalSection(CriticalSection)) {
        /* 待機者の数を増やしてから保持者を読む．解放側は保持者を消してから待機者の数を見るので，起こし損ねない */
        __atomic_add_fetch(waiter_count(CriticalSection), 1, __ATOMIC_SEQ_CST);
        HANDLE owner = __atomic_load_n(&CriticalSection->OwningThread, __ATOMIC_SEQ_CST);
        if (owner) RtlWaitOnAddress(&CriticalSection->OwningThread, &owner, sizeof(owner), NULL);
        __atomic_sub_fetch(waiter_count(CriticalSection), 1, __ATOMIC_SEQ_CST);
    }
    return NTMAC_STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlLeaveCriticalSection(PRTL_CRITICAL_SECTION CriticalSection) {
    if (--CriticalSection->RecursionCount > 0) return NTMAC_STATUS_SUCCESS;

    __atomic_sub_fetch(&CriticalSection->LockCount, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&CriticalSection->OwningThread, NULL, __ATOMIC_SEQ_CST);
    if (__atomic_load_n(waiter_count(CriticalSection), __ATOMIC_SEQ_CST)) {
        RtlWakeAddressSingle(&CriticalSection->OwningThread);
    }
    return NTMAC_STATUS_SUCCESS;
}
