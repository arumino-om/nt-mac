#include "ntdll/ntdll.h"
#include "shared/ntmachost.h"

#define TICKS_PER_SECOND   10000000LL
#define TICKS_PER_DAY      (TICKS_PER_SECOND * 86400)

NTSTATUS NTAPI NtQuerySystemTime(PLARGE_INTEGER SystemTime) {
    ntmachost_time_args args;
    NTSTATUS status = ntmachost_call(NTMACHOST_QUERY_SYSTEM_TIME, &args);
    SystemTime->QuadPart = args.SystemTime;
    return status;
}

NTSTATUS NTAPI NtQueryPerformanceCounter(PLARGE_INTEGER Counter, PLARGE_INTEGER Frequency) {
    ntmachost_perf_args args;
    NTSTATUS status = ntmachost_call(NTMACHOST_QUERY_PERFORMANCE_COUNTER, &args);
    Counter->QuadPart = args.Counter;
    if (Frequency) Frequency->QuadPart = args.Frequency;
    return status;
}

static int64_t time_zone_bias(void) {
    ntmachost_time_args args;
    ntmachost_call(NTMACHOST_QUERY_SYSTEM_TIME, &args);
    return args.TimeZoneBias;
}

NTSTATUS NTAPI RtlSystemTimeToLocalTime(const LARGE_INTEGER* SystemTime, PLARGE_INTEGER LocalTime) {
    LocalTime->QuadPart = SystemTime->QuadPart - time_zone_bias();
    return NTMAC_STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlLocalTimeToSystemTime(const LARGE_INTEGER* LocalTime, PLARGE_INTEGER SystemTime) {
    SystemTime->QuadPart = LocalTime->QuadPart + time_zone_bias();
    return NTMAC_STATUS_SUCCESS;
}

/* 1601-01-01 からの日数と，年月日の相互変換 (グレゴリオ暦) */

static int is_leap(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static const int days_before_month[2][13] = {
    {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365},
    {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366},
};

VOID NTAPI RtlTimeToTimeFields(const LARGE_INTEGER* Time, PTIME_FIELDS TimeFields) {
    int64_t t = Time->QuadPart;
    int64_t days = t / TICKS_PER_DAY;
    int64_t rest = t % TICKS_PER_DAY;

    TimeFields->Milliseconds = (SHORT)((rest % TICKS_PER_SECOND) / 10000);
    rest /= TICKS_PER_SECOND;
    TimeFields->Second = (SHORT)(rest % 60);
    TimeFields->Minute = (SHORT)((rest / 60) % 60);
    TimeFields->Hour = (SHORT)(rest / 3600);
    TimeFields->Weekday = (SHORT)((days + 1) % 7);  /* 1601-01-01 は月曜 */

    /* 400 年 = 146097 日の周期で年を求める */
    int year = 1601;
    year += (int)(days / 146097) * 400;
    days %= 146097;
    for (;;) {
        int len = is_leap(year) ? 366 : 365;
        if (days < len) break;
        days -= len;
        year++;
    }

    int leap = is_leap(year);
    int month = 1;
    while (days >= days_before_month[leap][month]) month++;
    TimeFields->Year = (SHORT)year;
    TimeFields->Month = (SHORT)month;
    TimeFields->Day = (SHORT)(days - days_before_month[leap][month - 1] + 1);
}

BOOLEAN NTAPI RtlTimeFieldsToTime(const TIME_FIELDS* TimeFields, PLARGE_INTEGER Time) {
    const TIME_FIELDS* f = TimeFields;
    if (f->Year < 1601 || f->Month < 1 || f->Month > 12 || f->Day < 1 || f->Hour < 0 || f->Hour > 23 ||
        f->Minute < 0 || f->Minute > 59 || f->Second < 0 || f->Second > 59 || f->Milliseconds < 0 ||
        f->Milliseconds > 999) {
        return FALSE;
    }
    int leap = is_leap(f->Year);
    if (f->Day > days_before_month[leap][f->Month] - days_before_month[leap][f->Month - 1]) return FALSE;

    int64_t y = f->Year - 1601;
    int64_t days = y * 365 + y / 4 - y / 100 + y / 400;
    days += days_before_month[leap][f->Month - 1] + f->Day - 1;
    Time->QuadPart = days * TICKS_PER_DAY +
                     ((int64_t)f->Hour * 3600 + f->Minute * 60 + f->Second) * TICKS_PER_SECOND +
                     (int64_t)f->Milliseconds * 10000;
    return TRUE;
}
