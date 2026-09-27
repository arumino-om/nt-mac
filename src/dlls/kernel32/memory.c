#include "kernel32_private.h"

HANDLE WINAPI GetProcessHeap(void) {
    return RtlGetCurrentPeb()->ProcessHeap;
}

/* RtlFreeHeap は BOOLEAN (1バイト) を返すので，BOOL に変換するためフォワードせずに包む */
BOOL WINAPI HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem) {
    if (RtlFreeHeap(hHeap, dwFlags, lpMem)) return TRUE;
    RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
    return FALSE;
}

LPVOID WINAPI VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect) {
    PVOID base = lpAddress;
    SIZE_T size = dwSize;
    NTSTATUS status = NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &size, flAllocationType, flProtect);
    return set_ntstatus(status) ? base : NULL;
}

BOOL WINAPI VirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType) {
    PVOID base = lpAddress;
    SIZE_T size = dwSize;
    return set_ntstatus(NtFreeVirtualMemory(NtCurrentProcess(), &base, &size, dwFreeType));
}

BOOL WINAPI VirtualProtect(LPVOID lpAddress, SIZE_T dwSize, DWORD flNewProtect, PDWORD lpflOldProtect) {
    PVOID base = lpAddress;
    SIZE_T size = dwSize;
    ULONG old = 0;
    NTSTATUS status = NtProtectVirtualMemory(NtCurrentProcess(), &base, &size, flNewProtect, &old);
    if (lpflOldProtect) *lpflOldProtect = old;
    return set_ntstatus(status);
}

SIZE_T WINAPI VirtualQuery(LPCVOID lpAddress, PMEMORY_BASIC_INFORMATION lpBuffer, SIZE_T dwLength) {
    SIZE_T result = 0;
    NTSTATUS status = NtQueryVirtualMemory(NtCurrentProcess(), (PVOID)lpAddress, MemoryBasicInformation,
                                           lpBuffer, dwLength, &result);
    return set_ntstatus(status) ? result : 0;
}

/* Local* / Global* はプロセスヒープの上に実装する (移動可能なメモリ (LMEM_MOVEABLE) も固定として扱う) */

HLOCAL WINAPI LocalAlloc(UINT uFlags, SIZE_T uBytes) {
    return RtlAllocateHeap(GetProcessHeap(), (uFlags & LMEM_ZEROINIT) ? HEAP_ZERO_MEMORY : 0, uBytes);
}

HLOCAL WINAPI LocalFree(HLOCAL hMem) {
    if (!hMem) return NULL;
    return RtlFreeHeap(GetProcessHeap(), 0, hMem) ? NULL : hMem;
}

HGLOBAL WINAPI GlobalAlloc(UINT uFlags, SIZE_T dwBytes) {
    return RtlAllocateHeap(GetProcessHeap(), (uFlags & GMEM_ZEROINIT) ? HEAP_ZERO_MEMORY : 0, dwBytes);
}

HGLOBAL WINAPI GlobalFree(HGLOBAL hMem) {
    if (!hMem) return NULL;
    return RtlFreeHeap(GetProcessHeap(), 0, hMem) ? NULL : hMem;
}
