#include <mach/mach_time.h>
#include <sched.h>
#include <sys/time.h>
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include "host/ntmachost/services.h"

namespace ntmac::ntmachost {

namespace {

// 1601-01-01 から 1970-01-01 までの 100ns 単位の差
constexpr int64_t kUnixEpochIn100ns = 116444736000000000LL;

int64_t now_in_100ns() {
    timeval tv{};
    gettimeofday(&tv, nullptr);
    return kUnixEpochIn100ns + static_cast<int64_t>(tv.tv_sec) * 10000000 + tv.tv_usec * 10;
}

}

void exit_process(uint32_t code) {
    std::exit(static_cast<int>(code));
}

ntmac_ntstatus delay_execution(int64_t interval) {
    int64_t relative = interval < 0 ? -interval : interval - now_in_100ns();
    if (relative <= 0) return yield_execution();

    timespec ts{
        .tv_sec = static_cast<time_t>(relative / 10000000),
        .tv_nsec = static_cast<long>(relative % 10000000) * 100,
    };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus yield_execution() {
    sched_yield();
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus query_system_time(ntmachost_time_args* args) {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    args->SystemTime = kUnixEpochIn100ns + static_cast<int64_t>(ts.tv_sec) * 10000000 + ts.tv_nsec / 100;

    tm local{};
    time_t now = ts.tv_sec;
    localtime_r(&now, &local);
    args->TimeZoneBias = -static_cast<int64_t>(local.tm_gmtoff) * 10000000;
    return NTMAC_STATUS_SUCCESS;
}

ntmac_ntstatus query_performance_counter(ntmachost_perf_args* args) {
    // Windows 10 以降と同じく 10MHz (100ns 単位) にする
    static const mach_timebase_info_data_t timebase = [] {
        mach_timebase_info_data_t tb{};
        mach_timebase_info(&tb);
        return tb;
    }();
    unsigned __int128 ns = static_cast<unsigned __int128>(mach_absolute_time()) * timebase.numer / timebase.denom;
    args->Counter = static_cast<int64_t>(ns / 100);
    args->Frequency = 10000000;
    return NTMAC_STATUS_SUCCESS;
}

}
