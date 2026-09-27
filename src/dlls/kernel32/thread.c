#include "kernel32_private.h"

/* TlsAlloc で割り当て済みのスロット (TEB->TlsSlots の 64 個のみ．TlsExpansionSlots は未対応) */
static volatile uint64_t tls_bitmap;

/* FlsAlloc で割り当て済みのスロットと，その解放時のコールバック */
#define FLS_SLOTS 128
static volatile uint64_t fls_bitmap[FLS_SLOTS / 64];
static PFLS_CALLBACK_FUNCTION fls_callbacks[FLS_SLOTS];

HANDLE WINAPI GetCurrentThread(void) {
    return (HANDLE)(LONG_PTR)-2;
}

DWORD WINAPI GetCurrentThreadId(void) {
    return (DWORD)ntmac_current_teb()->UniqueThread;
}

BOOL WINAPI InitializeCriticalSectionEx(LPCRITICAL_SECTION lpCriticalSection, DWORD dwSpinCount, DWORD Flags) {
    return set_ntstatus(RtlInitializeCriticalSectionEx(lpCriticalSection, dwSpinCount, Flags));
}

BOOL WINAPI InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION lpCriticalSection, DWORD dwSpinCount) {
    RtlInitializeCriticalSectionAndSpinCount(lpCriticalSection, dwSpinCount);
    return TRUE;
}

/* RtlTryEnterCriticalSection は BOOLEAN を返すので BOOL に変換する */
BOOL WINAPI TryEnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
    return RtlTryEnterCriticalSection(lpCriticalSection) ? TRUE : FALSE;
}

void WINAPI Sleep(DWORD dwMilliseconds) {
    LARGE_INTEGER interval;
    interval.QuadPart = -(LONGLONG)dwMilliseconds * 10000; /* 負の値は相対時間 (100ns 単位) */
    NtDelayExecution(FALSE, &interval);
}

DWORD WINAPI TlsAlloc(void) {
    uint64_t bits = __atomic_load_n(&tls_bitmap, __ATOMIC_RELAXED);
    for (;;) {
        if (bits == ~(uint64_t)0) {
            RtlSetLastWin32Error(ERROR_NO_MORE_ITEMS);
            return TLS_OUT_OF_INDEXES;
        }
        DWORD index = (DWORD)__builtin_ctzll(~bits);
        if (__atomic_compare_exchange_n(&tls_bitmap, &bits, bits | ((uint64_t)1 << index), FALSE,
                                        __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            ntmac_current_teb()->TlsSlots[index] = NULL; /* 他のスレッドのスロットはスレッド対応時に初期化する */
            return index;
        }
    }
}

BOOL WINAPI TlsFree(DWORD dwTlsIndex) {
    if (dwTlsIndex >= 64) {
        RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    __atomic_and_fetch(&tls_bitmap, ~((uint64_t)1 << dwTlsIndex), __ATOMIC_ACQ_REL);
    return TRUE;
}

LPVOID WINAPI TlsGetValue(DWORD dwTlsIndex) {
    if (dwTlsIndex >= 64) {
        RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    RtlSetLastWin32Error(ERROR_SUCCESS); /* 値が NULL の場合と区別できるよう，成功時は 0 にする */
    return ntmac_current_teb()->TlsSlots[dwTlsIndex];
}

BOOL WINAPI TlsSetValue(DWORD dwTlsIndex, LPVOID lpTlsValue) {
    if (dwTlsIndex >= 64) {
        RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ntmac_current_teb()->TlsSlots[dwTlsIndex] = lpTlsValue;
    return TRUE;
}

DWORD WINAPI GetCurrentProcessorNumber(void) {
    return 0;
}

BOOL WINAPI IsThreadAFiber(void) {
    return FALSE;
}

BOOL WINAPI SwitchToThread(void) {
    NtYieldExecution();
    return TRUE;
}

/* ---- 同期 (ntdll の関数の BOOL / ミリ秒版) ---- */

BOOL WINAPI SleepConditionVariableSRW(PCONDITION_VARIABLE ConditionVariable, PSRWLOCK SRWLock, DWORD dwMilliseconds,
                                      ULONG Flags) {
    LARGE_INTEGER buf;
    NTSTATUS status = RtlSleepConditionVariableSRW(ConditionVariable, SRWLock, ms_to_timeout(dwMilliseconds, &buf), Flags);
    if (status == NTMAC_STATUS_TIMEOUT) {
        RtlSetLastWin32Error(ERROR_TIMEOUT);
        return FALSE;
    }
    return set_ntstatus(status);
}

BOOL WINAPI SleepConditionVariableCS(PCONDITION_VARIABLE ConditionVariable, PCRITICAL_SECTION CriticalSection,
                                     DWORD dwMilliseconds) {
    LARGE_INTEGER buf;
    NTSTATUS status = RtlSleepConditionVariableCS(ConditionVariable, CriticalSection, ms_to_timeout(dwMilliseconds, &buf));
    if (status == NTMAC_STATUS_TIMEOUT) {
        RtlSetLastWin32Error(ERROR_TIMEOUT);
        return FALSE;
    }
    return set_ntstatus(status);
}

BOOL WINAPI InitOnceExecuteOnce(PINIT_ONCE InitOnce, PINIT_ONCE_FN InitFn, PVOID Parameter, LPVOID* Context) {
    return NT_SUCCESS(RtlRunOnceExecuteOnce(InitOnce, (PRTL_RUN_ONCE_INIT_FN)InitFn, Parameter, Context));
}

BOOL WINAPI WaitOnAddress(volatile VOID* Address, PVOID CompareAddress, SIZE_T AddressSize, DWORD dwMilliseconds) {
    LARGE_INTEGER buf;
    NTSTATUS status = RtlWaitOnAddress(Address, CompareAddress, AddressSize, ms_to_timeout(dwMilliseconds, &buf));
    if (status == NTMAC_STATUS_TIMEOUT) {
        RtlSetLastWin32Error(ERROR_TIMEOUT);
        return FALSE;
    }
    return set_ntstatus(status);
}

/* ---- FLS (ファイバーローカルストレージ)．ファイバーは未対応なので，スレッドごとの領域として扱う ---- */

static PVOID* fls_slots(BOOL create) {
    ntmac_teb* teb = ntmac_current_teb();
    if (!teb->FlsData && create) {
        teb->FlsData = RtlAllocateHeap(GetProcessHeap(), HEAP_ZERO_MEMORY, FLS_SLOTS * sizeof(PVOID));
    }
    return teb->FlsData;
}

DWORD WINAPI FlsAlloc(PFLS_CALLBACK_FUNCTION lpCallback) {
    for (DWORD i = 0; i < FLS_SLOTS; i++) {
        uint64_t bit = (uint64_t)1 << (i % 64);
        uint64_t old = __atomic_fetch_or(&fls_bitmap[i / 64], bit, __ATOMIC_ACQ_REL);
        if (old & bit) continue;
        fls_callbacks[i] = lpCallback;
        PVOID* slots = fls_slots(FALSE);
        if (slots) slots[i] = NULL;
        return i;
    }
    RtlSetLastWin32Error(ERROR_NO_MORE_ITEMS);
    return FLS_OUT_OF_INDEXES;
}

static BOOL fls_index_valid(DWORD index) {
    if (index < FLS_SLOTS && (fls_bitmap[index / 64] & ((uint64_t)1 << (index % 64)))) return TRUE;
    RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
    return FALSE;
}

PVOID WINAPI FlsGetValue(DWORD dwFlsIndex) {
    if (!fls_index_valid(dwFlsIndex)) return NULL;
    PVOID* slots = fls_slots(FALSE);
    RtlSetLastWin32Error(ERROR_SUCCESS);
    return slots ? slots[dwFlsIndex] : NULL;
}

BOOL WINAPI FlsSetValue(DWORD dwFlsIndex, PVOID lpFlsData) {
    if (!fls_index_valid(dwFlsIndex)) return FALSE;
    PVOID* slots = fls_slots(TRUE);
    if (!slots) {
        RtlSetLastWin32Error(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    slots[dwFlsIndex] = lpFlsData;
    return TRUE;
}

BOOL WINAPI FlsFree(DWORD dwFlsIndex) {
    if (!fls_index_valid(dwFlsIndex)) return FALSE;
    /* 本来は全スレッドの値についてコールバックを呼ぶが，今は呼び出したスレッドの分だけ */
    PVOID* slots = fls_slots(FALSE);
    if (slots && slots[dwFlsIndex] && fls_callbacks[dwFlsIndex]) fls_callbacks[dwFlsIndex](slots[dwFlsIndex]);
    if (slots) slots[dwFlsIndex] = NULL;
    fls_callbacks[dwFlsIndex] = NULL;
    __atomic_and_fetch(&fls_bitmap[dwFlsIndex / 64], ~((uint64_t)1 << (dwFlsIndex % 64)), __ATOMIC_ACQ_REL);
    return TRUE;
}
