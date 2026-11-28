/* Platform-backed wrappers for the double-precision ISO C entry points that
 * were absent from the C+ symbol surface (see docs/libc/ISO_C_FULL_INVENTORY.md).
 * Each delegates to the identically named host libm/stdio function; the host
 * is the source of truth for rounding, errno and FP-exception semantics.
 * No upstream algorithm is duplicated and no host symbol is interposed. */
#include "../../log/log.h"
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

double cplus_rint(double x) {
    cplus_libc_log("rint", "enter");
    return rint(x);
}
double cplus_nexttoward(double x, long double y) {
    cplus_libc_log("nexttoward", "enter");
    return nexttoward(x, y);
}
long cplus_lround(double x) {
    cplus_libc_log("lround", "enter");
    return lround(x);
}
long long cplus_llround(double x) {
    cplus_libc_log("llround", "enter");
    return llround(x);
}
double cplus_scalbln(double x, long n) {
    cplus_libc_log("scalbln", "enter");
    return scalbln(x, n);
}
int cplus_vscanf(const char *fmt, va_list ap) {
    cplus_libc_log("vscanf", "enter");
    if (!fmt) { errno = EINVAL; return EOF; }
    return vscanf(fmt, ap);
}
