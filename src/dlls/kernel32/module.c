#include "kernel32_private.h"

static BOOL set_ntstatus_bool(NTSTATUS status) {
    return set_ntstatus(status);
}

HMODULE WINAPI GetModuleHandleW(LPCWSTR lpModuleName) {
    if (!lpModuleName) return (HMODULE)RtlGetCurrentPeb()->ImageBaseAddress;

    UNICODE_STRING name;
    init_unicode_string(&name, lpModuleName);
    PVOID module = NULL;
    return set_ntstatus_bool(LdrGetDllHandle(NULL, NULL, &name, &module)) ? (HMODULE)module : NULL;
}

HMODULE WINAPI GetModuleHandleA(LPCSTR lpModuleName) {
    if (!lpModuleName) return GetModuleHandleW(NULL);
    WCHAR buf[MAX_PATH];
    if (!ansi_to_wide(lpModuleName, buf, MAX_PATH)) return NULL;
    return GetModuleHandleW(buf);
}

BOOL WINAPI GetModuleHandleExW(DWORD dwFlags, LPCWSTR lpModuleName, HMODULE* phModule) {
    if (!phModule) {
        RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (dwFlags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) {
        PVOID base;
        *phModule = RtlPcToFileHeader((PVOID)lpModuleName, &base) ? (HMODULE)base : NULL;
        if (!*phModule) RtlSetLastWin32Error(ERROR_MOD_NOT_FOUND);
    } else {
        *phModule = GetModuleHandleW(lpModuleName);
    }
    /* DLL の解放は未実装なので，参照カウントの指定 (PIN / UNCHANGED_REFCOUNT) は無視してよい */
    return *phModule != NULL;
}

BOOL WINAPI GetModuleHandleExA(DWORD dwFlags, LPCSTR lpModuleName, HMODULE* phModule) {
    if ((dwFlags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) || !lpModuleName) {
        return GetModuleHandleExW(dwFlags, (LPCWSTR)lpModuleName, phModule);
    }
    WCHAR buf[MAX_PATH];
    if (!ansi_to_wide(lpModuleName, buf, MAX_PATH)) return FALSE;
    return GetModuleHandleExW(dwFlags, buf, phModule);
}

DWORD WINAPI GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize) {
    if (nSize == 0) {
        RtlSetLastWin32Error(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    ULONG written = 0;
    NTSTATUS status = ntmac_LdrGetModuleFileName(hModule, lpFilename, nSize, &written);
    if (status == NTMAC_STATUS_BUFFER_TOO_SMALL) {
        /* 切り詰めた場合は nSize を返し，ERROR_INSUFFICIENT_BUFFER を設定する */
        RtlSetLastWin32Error(ERROR_INSUFFICIENT_BUFFER);
        return nSize;
    }
    if (!set_ntstatus(status)) return 0;
    RtlSetLastWin32Error(ERROR_SUCCESS);
    return written;
}

DWORD WINAPI GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize) {
    WCHAR buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(hModule, buf, MAX_PATH * 4);
    if (!n) return 0;
    int len = WideCharToMultiByte(CP_ACP, 0, buf, (int)n, lpFilename, (int)nSize, NULL, NULL);
    if (len <= 0 || (DWORD)len >= nSize) {
        if (nSize) lpFilename[nSize - 1] = 0;
        RtlSetLastWin32Error(ERROR_INSUFFICIENT_BUFFER);
        return nSize;
    }
    lpFilename[len] = 0;
    return (DWORD)len;
}

FARPROC WINAPI GetProcAddress(HMODULE hModule, LPCSTR lpProcName) {
    PVOID address = NULL;
    NTSTATUS status;
    if ((ULONG_PTR)lpProcName >> 16) {
        ANSI_STRING name = {.Length = (USHORT)strlen(lpProcName), .Buffer = (PCHAR)lpProcName};
        name.MaximumLength = name.Length;
        status = LdrGetProcedureAddress(hModule, &name, 0, &address);
    } else {
        status = LdrGetProcedureAddress(hModule, NULL, (ULONG)(ULONG_PTR)lpProcName, &address);
    }
    return set_ntstatus(status) ? (FARPROC)address : NULL;
}

HMODULE WINAPI LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    (void)hFile;
    (void)dwFlags; /* 探索パスの指定やデータファイルとしての読み込みは未対応 (通常の読み込みとして扱う) */
    if (!lpLibFileName) {
        RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    UNICODE_STRING name;
    init_unicode_string(&name, lpLibFileName);
    PVOID module = NULL;
    return set_ntstatus(LdrLoadDll(NULL, NULL, &name, &module)) ? (HMODULE)module : NULL;
}

HMODULE WINAPI LoadLibraryW(LPCWSTR lpLibFileName) {
    return LoadLibraryExW(lpLibFileName, NULL, 0);
}

HMODULE WINAPI LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    WCHAR buf[MAX_PATH];
    if (!lpLibFileName || !ansi_to_wide(lpLibFileName, buf, MAX_PATH)) {
        RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    return LoadLibraryExW(buf, hFile, dwFlags);
}

HMODULE WINAPI LoadLibraryA(LPCSTR lpLibFileName) {
    return LoadLibraryExA(lpLibFileName, NULL, 0);
}

BOOL WINAPI FreeLibrary(HMODULE hLibModule) {
    return set_ntstatus(LdrUnloadDll(hLibModule));
}
