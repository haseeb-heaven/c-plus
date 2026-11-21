/*
 * Platform-backed stdlib conversions (C+ libc slice). These delegate to the
 * installed system libc where re-implementing would duplicate mature, ABI-tied
 * behaviour (strtoll/strtoull/strtod locale + rounding). Logged per C+ convention.
 * Not vendored upstream; documented as platform-backed in docs/libc/COVERAGE.md.
 */
#include <stdlib.h>
#include "../../log/log.h"

long long cplus_strtoll(const char *restrict nptr, char **restrict endptr, int base) {
    cplus_libc_log("strtoll", "enter");
    return strtoll(nptr, endptr, base);
}
unsigned long long cplus_strtoull(const char *restrict nptr, char **restrict endptr, int base) {
    cplus_libc_log("strtoull", "enter");
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
