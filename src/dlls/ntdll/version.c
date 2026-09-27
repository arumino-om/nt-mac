#include "ntdll/ntdll.h"
#include "shared/ntstatus.h"

/* OS のバージョン．PEB の値 (ホストが Windows 10 22H2 として設定) を使う */

NTSTATUS NTAPI RtlGetVersion(PRTL_OSVERSIONINFOW Info) {
    ntmac_peb* peb = RtlGetCurrentPeb();
    Info->dwMajorVersion = peb->OSMajorVersion;
    Info->dwMinorVersion = peb->OSMinorVersion;
    Info->dwBuildNumber = peb->OSBuildNumber;
    Info->dwPlatformId = peb->OSPlatformId;
    Info->szCSDVersion[0] = 0;
    if (Info->dwOSVersionInfoSize >= sizeof(RTL_OSVERSIONINFOEXW)) {
        PRTL_OSVERSIONINFOEXW ex = (PRTL_OSVERSIONINFOEXW)Info;
        ex->wServicePackMajor = 0;
        ex->wServicePackMinor = 0;
        ex->wSuiteMask = VER_SUITE_SINGLEUSERTS;
        ex->wProductType = VER_NT_WORKSTATION;
        ex->wReserved = 0;
    }
    return NTMAC_STATUS_SUCCESS;
}

/* 条件は種類ごとに3ビットずつ並ぶ (VER_MINORVERSION が最下位) */
static int condition_shift(DWORD type) {
    int index = 0;
    while (type > 1) type >>= 1, index++;
    return index * 3;
}

ULONGLONG NTAPI VerSetConditionMask(ULONGLONG ConditionMask, DWORD TypeMask, BYTE Condition) {
    for (DWORD type = 1; type <= VER_PRODUCT_TYPE; type <<= 1) {
        if (TypeMask & type) {
            ConditionMask &= ~((ULONGLONG)7 << condition_shift(type));
            ConditionMask |= (ULONGLONG)(Condition & 7) << condition_shift(type);
        }
    }
    return ConditionMask;
}

static BYTE condition_of(ULONGLONG mask, DWORD type) {
    return (BYTE)((mask >> condition_shift(type)) & 7);
}

static BOOL compare(ULONGLONG actual, ULONGLONG required, BYTE condition) {
    switch (condition) {
    case VER_EQUAL:         return actual == required;
    case VER_GREATER:       return actual > required;
    case VER_GREATER_EQUAL: return actual >= required;
    case VER_LESS:          return actual < required;
    case VER_LESS_EQUAL:    return actual <= required;
    default:                return FALSE;
    }
}

NTSTATUS NTAPI RtlVerifyVersionInfo(PRTL_OSVERSIONINFOEXW Info, ULONG TypeMask, ULONGLONG ConditionMask) {
    RTL_OSVERSIONINFOEXW current = {.dwOSVersionInfoSize = sizeof(current)};
    RtlGetVersion((PRTL_OSVERSIONINFOW)&current);

    if (!TypeMask || !ConditionMask) return NTMAC_STATUS_INVALID_PARAMETER;

    /* メジャー → マイナー → SP メジャー → SP マイナーは，まとめて辞書式に比べる．
     * 条件には指定された中で最上位のものを使う (Windows と同じ) */
    static const DWORD version_types[] = {VER_MAJORVERSION, VER_MINORVERSION, VER_SERVICEPACKMAJOR, VER_SERVICEPACKMINOR};
    ULONGLONG actual = 0, required = 0;
    BYTE condition = 0;
    for (int i = 0; i < 4; i++) {
        DWORD type = version_types[i];
        ULONGLONG a = 0, r = 0;
        switch (type) {
        case VER_MAJORVERSION:     a = current.dwMajorVersion;    r = Info->dwMajorVersion; break;
        case VER_MINORVERSION:     a = current.dwMinorVersion;    r = Info->dwMinorVersion; break;
        case VER_SERVICEPACKMAJOR: a = current.wServicePackMajor; r = Info->wServicePackMajor; break;
        default:                   a = current.wServicePackMinor; r = Info->wServicePackMinor; break;
        }
        actual = (actual << 16) | a;
        required = (required << 16) | ((TypeMask & type) ? r : a);
        if ((TypeMask & type) && !condition) condition = condition_of(ConditionMask, type);
    }
    if (condition && !compare(actual, required, condition)) return NTMAC_STATUS_REVISION_MISMATCH;

    if ((TypeMask & VER_BUILDNUMBER) &&
        !compare(current.dwBuildNumber, Info->dwBuildNumber, condition_of(ConditionMask, VER_BUILDNUMBER))) {
        return NTMAC_STATUS_REVISION_MISMATCH;
    }
    if ((TypeMask & VER_PLATFORMID) &&
        !compare(current.dwPlatformId, Info->dwPlatformId, condition_of(ConditionMask, VER_PLATFORMID))) {
        return NTMAC_STATUS_REVISION_MISMATCH;
    }
    if ((TypeMask & VER_PRODUCT_TYPE) &&
        !compare(current.wProductType, Info->wProductType, condition_of(ConditionMask, VER_PRODUCT_TYPE))) {
        return NTMAC_STATUS_REVISION_MISMATCH;
    }
    return NTMAC_STATUS_SUCCESS;
}
