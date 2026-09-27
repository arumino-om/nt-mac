#include "kernel32_private.h"

static void to_filetime(const LARGE_INTEGER* t, LPFILETIME ft) {
    ft->dwLowDateTime = t->LowPart;
    ft->dwHighDateTime = (DWORD)t->HighPart;
}

static LARGE_INTEGER from_filetime(const FILETIME* ft) {
    LARGE_INTEGER t;
    t.LowPart = ft->dwLowDateTime;
    t.HighPart = (LONG)ft->dwHighDateTime;
    return t;
}

static void to_systemtime(const LARGE_INTEGER* t, LPSYSTEMTIME st) {
    TIME_FIELDS f;
    RtlTimeToTimeFields(t, &f);
    st->wYear = (WORD)f.Year;
    st->wMonth = (WORD)f.Month;
    st->wDayOfWeek = (WORD)f.Weekday;
    st->wDay = (WORD)f.Day;
    st->wHour = (WORD)f.Hour;
    st->wMinute = (WORD)f.Minute;
    st->wSecond = (WORD)f.Second;
    st->wMilliseconds = (WORD)f.Milliseconds;
}

void WINAPI GetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime) {
    LARGE_INTEGER now;
    NtQuerySystemTime(&now);
    to_filetime(&now, lpSystemTimeAsFileTime);
}

void WINAPI GetSystemTimePreciseAsFileTime(LPFILETIME lpSystemTimeAsFileTime) {
    GetSystemTimeAsFileTime(lpSystemTimeAsFileTime);
}

void WINAPI GetSystemTime(LPSYSTEMTIME lpSystemTime) {
    LARGE_INTEGER now;
    NtQuerySystemTime(&now);
    to_systemtime(&now, lpSystemTime);
}

void WINAPI GetLocalTime(LPSYSTEMTIME lpSystemTime) {
    LARGE_INTEGER now, local;
    NtQuerySystemTime(&now);
    RtlSystemTimeToLocalTime(&now, &local);
    to_systemtime(&local, lpSystemTime);
}

BOOL WINAPI FileTimeToSystemTime(const FILETIME* lpFileTime, LPSYSTEMTIME lpSystemTime) {
    LARGE_INTEGER t = from_filetime(lpFileTime);
    if (t.QuadPart < 0) {
        RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    to_systemtime(&t, lpSystemTime);
    return TRUE;
}

BOOL WINAPI SystemTimeToFileTime(const SYSTEMTIME* lpSystemTime, LPFILETIME lpFileTime) {
    TIME_FIELDS f = {
        .Year = (SHORT)lpSystemTime->wYear, .Month = (SHORT)lpSystemTime->wMonth, .Day = (SHORT)lpSystemTime->wDay,
        .Hour = (SHORT)lpSystemTime->wHour, .Minute = (SHORT)lpSystemTime->wMinute,
        .Second = (SHORT)lpSystemTime->wSecond, .Milliseconds = (SHORT)lpSystemTime->wMilliseconds,
    };
    LARGE_INTEGER t;
    if (!RtlTimeFieldsToTime(&f, &t)) {
        RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    to_filetime(&t, lpFileTime);
    return TRUE;
}

BOOL WINAPI FileTimeToLocalFileTime(const FILETIME* lpFileTime, LPFILETIME lpLocalFileTime) {
    LARGE_INTEGER t = from_filetime(lpFileTime), local;
    RtlSystemTimeToLocalTime(&t, &local);
    to_filetime(&local, lpLocalFileTime);
    return TRUE;
}

BOOL WINAPI LocalFileTimeToFileTime(const FILETIME* lpLocalFileTime, LPFILETIME lpFileTime) {
    LARGE_INTEGER local = from_filetime(lpLocalFileTime), t;
    RtlLocalTimeToSystemTime(&local, &t);
    to_filetime(&t, lpFileTime);
    return TRUE;
}

BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount) {
    NtQueryPerformanceCounter(lpPerformanceCount, NULL);
    return TRUE;
}

BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER* lpFrequency) {
    LARGE_INTEGER counter;
    NtQueryPerformanceCounter(&counter, lpFrequency);
    return TRUE;
}

ULONGLONG WINAPI GetTickCount64(void) {
    LARGE_INTEGER counter, frequency;
    NtQueryPerformanceCounter(&counter, &frequency);
    return (ULONGLONG)counter.QuadPart / ((ULONGLONG)frequency.QuadPart / 1000);
}

DWORD WINAPI GetTickCount(void) {
    return (DWORD)GetTickCount64();
}

/* 夏時間は扱わず，現在の UTC との差だけを返す */
DWORD WINAPI GetTimeZoneInformation(LPTIME_ZONE_INFORMATION lpTimeZoneInformation) {
    LARGE_INTEGER utc = {0}, local;
    NtQuerySystemTime(&utc);
    RtlSystemTimeToLocalTime(&utc, &local);

    memset(lpTimeZoneInformation, 0, sizeof(*lpTimeZoneInformation));
    lpTimeZoneInformation->Bias = (LONG)((utc.QuadPart - local.QuadPart) / (10000000LL * 60));
    return TIME_ZONE_ID_UNKNOWN;
}
