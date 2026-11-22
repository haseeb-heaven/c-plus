/* Platform-backed time extras (C+ libc slice). Delegate to the installed
 * system libc; logged per C+ convention. Covers the POSIX clock_gettime()/
 * nanosleep() pair and the C11 timespec_get(). */
#define _DEFAULT_SOURCE
#include "../../log/log.h"
#include <time.h>

int cplus_clock_gettime(clockid_t clk_id, struct timespec *tp) {
    cplus_libc_log("clock_gettime", "enter");
    return clock_gettime(clk_id, tp);
}
int cplus_nanosleep(const struct timespec *req, struct timespec *rem) {
    cplus_libc_log("nanosleep", "enter");
    return nanosleep(req, rem);
}
int cplus_timespec_get(struct timespec *ts, int base) {
    cplus_libc_log("timespec_get", "enter");
    return timespec_get(ts, base);
}
