#include "kernel32_private.h"

static LPTOP_LEVEL_EXCEPTION_FILTER unhandled_exception_filter;
static UINT error_mode;
static char* command_line_a;

HANDLE WINAPI GetCurrentProcess(void) {
    return NtCurrentProcess();
}

DWORD WINAPI GetCurrentProcessId(void) {
    return (DWORD)ntmac_current_teb()->UniqueProcess;
}

/* 例外のディスパッチ (SEH) は未実装なので，登録だけ受け付ける */
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter) {
    return __atomic_exchange_n(&unhandled_exception_filter, lpTopLevelExceptionFilter, __ATOMIC_ACQ_REL);
}

LONG WINAPI UnhandledExceptionFilter(struct _EXCEPTION_POINTERS* ExceptionInfo) {
    (void)ExceptionInfo;
    return EXCEPTION_CONTINUE_SEARCH;
}

BOOL WINAPI TerminateProcess(HANDLE hProcess, UINT uExitCode) {
    return set_ntstatus(NtTerminateProcess(hProcess, (NTSTATUS)uExitCode));
}

/* ---- 起動時の情報 ---- */

void WINAPI GetStartupInfoW(LPSTARTUPINFOW lpStartupInfo) {
    ntmac_process_parameters* params = RtlGetCurrentPeb()->ProcessParameters;
    memset(lpStartupInfo, 0, sizeof(*lpStartupInfo));
    lpStartupInfo->cb = sizeof(*lpStartupInfo);
    lpStartupInfo->hStdInput = params->StandardInput;
    lpStartupInfo->hStdOutput = params->StandardOutput;
    lpStartupInfo->hStdError = params->StandardError;
}

void WINAPI GetStartupInfoA(LPSTARTUPINFOA lpStartupInfo) {
    STARTUPINFOW w;
    GetStartupInfoW(&w);
    memset(lpStartupInfo, 0, sizeof(*lpStartupInfo));
    lpStartupInfo->cb = sizeof(*lpStartupInfo);
    lpStartupInfo->hStdInput = w.hStdInput;
    lpStartupInfo->hStdOutput = w.hStdOutput;
    lpStartupInfo->hStdError = w.hStdError;
}

LPWSTR WINAPI GetCommandLineW(void) {
    return (LPWSTR)RtlGetCurrentPeb()->ProcessParameters->CommandLine.Buffer;
}

LPSTR WINAPI GetCommandLineA(void) {
    if (!command_line_a) {
        LPCWSTR w = GetCommandLineW();
        int n = WideCharToMultiByte(CP_ACP, 0, w, -1, NULL, 0, NULL, NULL);
        char* s = RtlAllocateHeap(GetProcessHeap(), 0, (SIZE_T)n);
        if (!s) return NULL;
        WideCharToMultiByte(CP_ACP, 0, w, -1, s, n, NULL, NULL);
        char* expected = NULL;
        if (!__atomic_compare_exchange_n(&command_line_a, &expected, s, FALSE, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            RtlFreeHeap(GetProcessHeap(), 0, s); /* 他のスレッドが先に作った */
        }
    }
    return command_line_a;
}

/* ---- 環境変数 ---- */

static size_t environment_block_length(LPCWSTR env) {
    const WCHAR* p = env;
    while (*p) p += wcslen(p) + 1;
    return (size_t)(p - env) + 1;
}

LPWCH WINAPI GetEnvironmentStringsW(void) {
    LPCWSTR env = RtlGetCurrentPeb()->ProcessParameters->Environment;
    static const WCHAR empty[2] = {0, 0};
    if (!env) env = empty;

    size_t len = environment_block_length(env);
    LPWCH copy = RtlAllocateHeap(GetProcessHeap(), 0, len * sizeof(WCHAR));
    if (!copy) {
        RtlSetLastWin32Error(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    memcpy(copy, env, len * sizeof(WCHAR));
    return copy;
}

BOOL WINAPI FreeEnvironmentStringsW(LPWCH penv) {
    return RtlFreeHeap(GetProcessHeap(), 0, penv) ? TRUE : FALSE;
}

/* ---- システムの情報 ---- */

void WINAPI GetSystemInfo(LPSYSTEM_INFO lpSystemInfo) {
    memset(lpSystemInfo, 0, sizeof(*lpSystemInfo));
    lpSystemInfo->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64;
    lpSystemInfo->dwPageSize = 0x1000;
    lpSystemInfo->lpMinimumApplicationAddress = (LPVOID)0x10000;
    lpSystemInfo->lpMaximumApplicationAddress = (LPVOID)0x7FFFFFFEFFFF;
    DWORD count = RtlGetCurrentPeb()->NumberOfProcessors;
    lpSystemInfo->dwActiveProcessorMask = count >= 64 ? ~(DWORD_PTR)0 : (((DWORD_PTR)1 << count) - 1);
    lpSystemInfo->dwNumberOfProcessors = count;
    lpSystemInfo->dwProcessorType = PROCESSOR_AMD_X8664;
    lpSystemInfo->dwAllocationGranularity = 0x10000;
    lpSystemInfo->wProcessorLevel = 6;
}

void WINAPI GetNativeSystemInfo(LPSYSTEM_INFO lpSystemInfo) {
    GetSystemInfo(lpSystemInfo);
}

BOOL WINAPI IsProcessorFeaturePresent(DWORD ProcessorFeature) {
    return RtlIsProcessorFeaturePresent(ProcessorFeature) ? TRUE : FALSE;
}

BOOL WINAPI IsDebuggerPresent(void) {
    return RtlGetCurrentPeb()->BeingDebugged;
}

UINT WINAPI SetErrorMode(UINT uMode) {
    return __atomic_exchange_n(&error_mode, uMode, __ATOMIC_ACQ_REL);
}

UINT WINAPI GetErrorMode(void) {
    return error_mode;
}

/* デバッガへの出力．デバッガは無いので捨てる */
void WINAPI OutputDebugStringA(LPCSTR lpOutputString) {
    (void)lpOutputString;
}

void WINAPI OutputDebugStringW(LPCWSTR lpOutputString) {
    (void)lpOutputString;
}

/* ---- バージョン ---- */

BOOL WINAPI VerifyVersionInfoW(LPOSVERSIONINFOEXW lpVersionInformation, DWORD dwTypeMask, DWORDLONG dwlConditionMask) {
    NTSTATUS status = RtlVerifyVersionInfo((PRTL_OSVERSIONINFOEXW)lpVersionInformation, dwTypeMask, dwlConditionMask);
    if (status == NTMAC_STATUS_REVISION_MISMATCH) {
        RtlSetLastWin32Error(ERROR_OLD_WIN_VERSION);
        return FALSE;
    }
    return set_ntstatus(status);
}

BOOL WINAPI GetVersionExW(LPOSVERSIONINFOW lpVersionInformation) {
    if (lpVersionInformation->dwOSVersionInfoSize != sizeof(OSVERSIONINFOW) &&
        lpVersionInformation->dwOSVersionInfoSize != sizeof(OSVERSIONINFOEXW)) {
        RtlSetLastWin32Error(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    return set_ntstatus(RtlGetVersion((PRTL_OSVERSIONINFOW)lpVersionInformation));
}

BOOL WINAPI GetVersionExA(LPOSVERSIONINFOA lpVersionInformation) {
    OSVERSIONINFOEXW w = {.dwOSVersionInfoSize = sizeof(w)};
    BOOL ex = lpVersionInformation->dwOSVersionInfoSize == sizeof(OSVERSIONINFOEXA);
    if (!ex && lpVersionInformation->dwOSVersionInfoSize != sizeof(OSVERSIONINFOA)) {
        RtlSetLastWin32Error(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    RtlGetVersion((PRTL_OSVERSIONINFOW)&w);
    lpVersionInformation->dwMajorVersion = w.dwMajorVersion;
    lpVersionInformation->dwMinorVersion = w.dwMinorVersion;
    lpVersionInformation->dwBuildNumber = w.dwBuildNumber;
    lpVersionInformation->dwPlatformId = w.dwPlatformId;
    lpVersionInformation->szCSDVersion[0] = 0;
    if (ex) {
        LPOSVERSIONINFOEXA a = (LPOSVERSIONINFOEXA)lpVersionInformation;
        a->wServicePackMajor = w.wServicePackMajor;
        a->wServicePackMinor = w.wServicePackMinor;
        a->wSuiteMask = w.wSuiteMask;
        a->wProductType = w.wProductType;
        a->wReserved = 0;
    }
    return TRUE;
}

DWORD WINAPI GetVersion(void) {
    ntmac_peb* peb = RtlGetCurrentPeb();
    return (peb->OSMajorVersion & 0xFF) | ((peb->OSMinorVersion & 0xFF) << 8) | ((DWORD)peb->OSBuildNumber << 16);
}

/* nNumber * nNumerator / nDenominator を 64 ビットで計算し，四捨五入する */
int WINAPI MulDiv(int nNumber, int nNumerator, int nDenominator) {
    if (nDenominator == 0) return -1;
    LONGLONG product = (LONGLONG)nNumber * nNumerator;
    LONGLONG half = (nDenominator < 0 ? -nDenominator : nDenominator) / 2;
    LONGLONG result = ((product < 0) != (nDenominator < 0)) ? (product - (product < 0 ? half : -half)) / nDenominator
                                                           : (product + (product < 0 ? -half : half)) / nDenominator;
    if (result > 0x7FFFFFFF || result < -0x7FFFFFFFLL - 1) return -1;
    return (int)result;
}
