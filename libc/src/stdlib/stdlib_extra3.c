/*
 * Platform-backed stdlib conversions (C+ libc slice). These delegate to the
 * installed system libc where re-implementing would duplicate mature, ABI-tied
 * behaviour (strtoll/strtoull/strtod locale + rounding). Logged per C+ convention.
 * Not vendored upstream; documented as platform-backed in docs/libc/COVERAGE.md.
 */
#include <stdlib.h>
#include <errno.h>
#include "../../log/log.h"

long long cplus_strtoll(const char *restrict nptr, char **restrict endptr, int base) {
    cplus_libc_log("strtoll", "enter");
    if (!nptr) { if (endptr) *endptr = NULL; errno = EINVAL; return 0; }
    if (base < 0 || base == 1 || base > 36) {
        if (endptr) *endptr = (char *)nptr;
        errno = EINVAL;
        return 0;
    }
    return strtoll(nptr, endptr, base);
}
unsigned long long cplus_strtoull(const char *restrict nptr, char **restrict endptr, int base) {
    cplus_libc_log("strtoull", "enter");
    if (!nptr) { if (endptr) *endptr = NULL; errno = EINVAL; return 0; }
    if (base < 0 || base == 1 || base > 36) {
        if (endptr) *endptr = (char *)nptr;
        errno = EINVAL;
        return 0;
    }
    return strtoull(nptr, endptr, base);
}
double cplus_strtod(const char *restrict nptr, char **restrict endptr) {
    cplus_libc_log("strtod", "enter");
    return strtod(nptr, endptr);
}
double cplus_atof(const char *nptr) {
    cplus_libc_log("atof", "enter");
    return atof(nptr);
}
