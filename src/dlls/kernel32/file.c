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

BOOL WINAPI SetStdHandle(DWORD nStdHandle, HANDLE hHandle) {
    ntmac_process_parameters* params = RtlGetCurrentPeb()->ProcessParameters;
    switch (nStdHandle) {
    case STD_INPUT_HANDLE:  params->StandardInput = hHandle; return TRUE;
    case STD_OUTPUT_HANDLE: params->StandardOutput = hHandle; return TRUE;
    case STD_ERROR_HANDLE:  params->StandardError = hHandle; return TRUE;
    default:
        RtlSetLastWin32Error(ERROR_INVALID_HANDLE);
        return FALSE;
    }
}

BOOL WINAPI CloseHandle(HANDLE hObject) {
    return set_ntstatus(NtClose(hObject));
}

DWORD WINAPI GetFileType(HANDLE hFile) {
    IO_STATUS_BLOCK iosb;
    FILE_FS_DEVICE_INFORMATION info;
    if (!set_ntstatus(NtQueryVolumeInformationFile(hFile, &iosb, &info, sizeof(info), FileFsDeviceInformation))) {
        return FILE_TYPE_UNKNOWN;
    }
    RtlSetLastWin32Error(ERROR_SUCCESS);
    switch (info.DeviceType) {
    case FILE_DEVICE_DISK:       return FILE_TYPE_DISK;
    case FILE_DEVICE_CONSOLE:    return FILE_TYPE_CHAR;
    case FILE_DEVICE_NAMED_PIPE: return FILE_TYPE_PIPE;
    default:                     return FILE_TYPE_UNKNOWN;
    }
}

/*
 * コンソール．Windows のコンソールは無いので，端末も通常のファイルとして扱わせる
 * (GetConsoleMode が失敗すると，CRT は WriteConsoleW ではなく WriteFile で出力する)．
 */
BOOL WINAPI GetConsoleMode(HANDLE hConsoleHandle, LPDWORD lpMode) {
    (void)hConsoleHandle;
    if (lpMode) *lpMode = 0;
    RtlSetLastWin32Error(ERROR_INVALID_HANDLE);
    return FALSE;
}

BOOL WINAPI WriteConsoleW(HANDLE hConsoleOutput, const VOID* lpBuffer, DWORD nNumberOfCharsToWrite,
                          LPDWORD lpNumberOfCharsWritten, LPVOID lpReserved) {
    (void)lpReserved;
    if (lpNumberOfCharsWritten) *lpNumberOfCharsWritten = 0;
    int n = WideCharToMultiByte(CP_UTF8, 0, lpBuffer, (int)nNumberOfCharsToWrite, NULL, 0, NULL, NULL);
    if (n <= 0) return nNumberOfCharsToWrite == 0;

    char* buf = RtlAllocateHeap(GetProcessHeap(), 0, (SIZE_T)n);
    if (!buf) {
        RtlSetLastWin32Error(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    WideCharToMultiByte(CP_UTF8, 0, lpBuffer, (int)nNumberOfCharsToWrite, buf, n, NULL, NULL);
    DWORD written;
    BOOL ok = WriteFile(hConsoleOutput, buf, (DWORD)n, &written, NULL);
    RtlFreeHeap(GetProcessHeap(), 0, buf);
    if (ok && lpNumberOfCharsWritten) *lpNumberOfCharsWritten = nNumberOfCharsToWrite;
    return ok;
}
