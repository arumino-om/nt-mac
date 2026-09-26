#include "kernel32_private.h"

HANDLE WINAPI GetStdHandle(DWORD nStdHandle) {
    ntmac_process_parameters* params = RtlGetCurrentPeb()->ProcessParameters;
    switch (nStdHandle) {
    case STD_INPUT_HANDLE:  return params->StandardInput;
    case STD_OUTPUT_HANDLE: return params->StandardOutput;
    case STD_ERROR_HANDLE:  return params->StandardError;
    default:
        RtlSetLastWin32Error(ERROR_INVALID_HANDLE);
        return INVALID_HANDLE_VALUE;
    }
}

BOOL WINAPI WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
                      LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped) {
    if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = 0;
    if (lpOverlapped) {
        RtlSetLastWin32Error(ERROR_INVALID_PARAMETER); /* 非同期 I/O は未対応 */
        return FALSE;
    }

    IO_STATUS_BLOCK iosb = {0};
    NTSTATUS status = NtWriteFile(hFile, NULL, NULL, NULL, &iosb, (PVOID)lpBuffer, nNumberOfBytesToWrite, NULL, NULL);
    if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = (DWORD)iosb.Information;
    return set_ntstatus(status);
}

BOOL WINAPI ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead,
                     LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped) {
    if (lpNumberOfBytesRead) *lpNumberOfBytesRead = 0;
    if (lpOverlapped) {
        RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    IO_STATUS_BLOCK iosb = {0};
    NTSTATUS status = NtReadFile(hFile, NULL, NULL, NULL, &iosb, lpBuffer, nNumberOfBytesToRead, NULL, NULL);
    if (lpNumberOfBytesRead) *lpNumberOfBytesRead = (DWORD)iosb.Information;

    /* ReadFile ではファイル終端は成功 (読み込んだバイト数が0) として扱う */
    if (status == NTMAC_STATUS_END_OF_FILE) return TRUE;
    return set_ntstatus(status);
}
