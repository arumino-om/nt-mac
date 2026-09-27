#include "kernel32_private.h"

/* HRSRC は IMAGE_RESOURCE_DATA_ENTRY*，HGLOBAL (LoadResource の戻り値) はリソースのデータへのポインタ */

static HMODULE module_or_exe(HMODULE module) {
    return module ? module : (HMODULE)RtlGetCurrentPeb()->ImageBaseAddress;
}

HRSRC WINAPI FindResourceExW(HMODULE hModule, LPCWSTR lpType, LPCWSTR lpName, WORD wLanguage) {
    ULONG_PTR info[3] = {(ULONG_PTR)lpType, (ULONG_PTR)lpName, wLanguage};
    IMAGE_RESOURCE_DATA_ENTRY* entry = NULL;
    if (!set_ntstatus(LdrFindResource_U(module_or_exe(hModule), info, 2, &entry))) return NULL;
    return (HRSRC)entry;
}

HRSRC WINAPI FindResourceW(HMODULE hModule, LPCWSTR lpName, LPCWSTR lpType) {
    return FindResourceExW(hModule, lpType, lpName, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
}

HGLOBAL WINAPI LoadResource(HMODULE hModule, HRSRC hResInfo) {
    if (!hResInfo) {
        RtlSetLastWin32Error(ERROR_INVALID_HANDLE);
        return NULL;
    }
    PVOID data = NULL;
    if (!set_ntstatus(LdrAccessResource(module_or_exe(hModule), (IMAGE_RESOURCE_DATA_ENTRY*)hResInfo, &data, NULL))) {
        return NULL;
    }
    return data;
}

LPVOID WINAPI LockResource(HGLOBAL hResData) {
    return hResData;
}

BOOL WINAPI FreeResource(HGLOBAL hResData) {
    (void)hResData;
    return FALSE;
}

DWORD WINAPI SizeofResource(HMODULE hModule, HRSRC hResInfo) {
    (void)hModule;
    if (!hResInfo) {
        RtlSetLastWin32Error(ERROR_INVALID_HANDLE);
        return 0;
    }
    return ((IMAGE_RESOURCE_DATA_ENTRY*)hResInfo)->Size;
}
