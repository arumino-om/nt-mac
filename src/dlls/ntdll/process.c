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

ntmac_peb* NTAPI RtlGetCurrentPeb(void) {
    return ntmac_current_teb()->ProcessEnvironmentBlock;
}
