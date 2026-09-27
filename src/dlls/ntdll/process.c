#include "ntdll/ntdll.h"
#include "shared/ntmachost.h"

NTSTATUS NTAPI NtTerminateProcess(HANDLE ProcessHandle, NTSTATUS ExitStatus) {
    /* 他のプロセスの終了は未対応 */
    if (ProcessHandle != NULL && ProcessHandle != NtCurrentProcess()) {
        return NTMAC_STATUS_NOT_IMPLEMENTED;
    }

    ntmachost_exit_args args = {.ExitCode = (uint32_t)ExitStatus};
    return ntmachost_call(NTMACHOST_EXIT_PROCESS, &args);
}

/* 全 DLL の終了処理 (DllMain の DLL_PROCESS_DETACH) を行う */
VOID NTAPI LdrShutdownProcess(void) {
    ntmachost_call(NTMACHOST_SHUTDOWN_PROCESS, NULL);
}

/* ExitProcess の本体 */
VOID NTAPI RtlExitUserProcess(NTSTATUS ExitStatus) {
    LdrShutdownProcess();
    NtTerminateProcess(NtCurrentProcess(), ExitStatus);
    __builtin_unreachable();
}

NTSTATUS NTAPI NtDelayExecution(BOOLEAN Alertable, PLARGE_INTEGER DelayInterval) {
    (void)Alertable; /* APC は未実装 */
    ntmachost_delay_args args = {.Interval = DelayInterval->QuadPart};
    return ntmachost_call(NTMACHOST_DELAY_EXECUTION, &args);
}

NTSTATUS NTAPI NtYieldExecution(void) {
    return ntmachost_call(NTMACHOST_YIELD_EXECUTION, NULL);
}

ntmac_peb* NTAPI RtlGetCurrentPeb(void) {
    return ntmac_current_teb()->ProcessEnvironmentBlock;
}
