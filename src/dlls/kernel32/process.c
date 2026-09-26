#include "kernel32_private.h"

void WINAPI ExitProcess(UINT uExitCode) {
    NtTerminateProcess(NtCurrentProcess(), (NTSTATUS)uExitCode);
    __builtin_unreachable();
}
