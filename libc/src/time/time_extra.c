/* Platform-backed time extras (C+ libc slice). Delegate to the installed
 * system libc; logged per C+ convention. Covers the POSIX clock_gettime()/
 * nanosleep() pair and the C11 timespec_get(). */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#if !defined(__APPLE__)
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#  ifndef _ISOC11_SOURCE
#    define _ISOC11_SOURCE
#  endif
#endif
#include "../../log/log.h"
#include <errno.h>
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
struct tm *cplus_gmtime_r(const time_t *timep, struct tm *result) {
    cplus_libc_log("gmtime_r", "enter");
    if (!timep || !result) { errno = EINVAL; return NULL; }
    return gmtime_r(timep, result);
}
struct tm *cplus_localtime_r(const time_t *timep, struct tm *result) {
    cplus_libc_log("localtime_r", "enter");
    if (!timep || !result) { errno = EINVAL; return NULL; }
    return localtime_r(timep, result);
}
char *cplus_asctime_r(const struct tm *tm, char *buf) {
    cplus_libc_log("asctime_r", "enter");
    if (!tm || !buf) { errno = EINVAL; return NULL; }
    return asctime_r(tm, buf);
}
char *cplus_ctime_r(const time_t *timep, char *buf) {
    cplus_libc_log("ctime_r", "enter");
    if (!timep || !buf) { errno = EINVAL; return NULL; }
    return ctime_r(timep, buf);
}
