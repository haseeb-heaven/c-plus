/* Platform-backed ISO C <time.h> ASCII conversion wrappers (C+ libc slice).
 *
 * These are thin wrappers that delegate directly to the installed host libc
 * backend; they are NOT a bundled reimplementation of the conversion logic:
 *   - cplus_asctime -> host asctime()
 *   - cplus_ctime   -> host ctime()
 * All formatting and return semantics come from the host backend. Standard
 * signatures, output format and return values are preserved exactly, and no
 * allocations are performed here.
 *
 * IMPORTANT: asctime()/ctime() are inherently NON-REENTRANT by the C standard:
 * they write into a single shared static buffer that may be overwritten by a
 * later call. They are NOT thread-safe and must not be described as reentrant.
 * The reentrant alternatives cplus_asctime_r() / cplus_ctime_r() already exist
 * in libc/src/time/time_extra.c and are PREFERRED for all new code.
 *
 * A NULL pointer argument is a defined hardening extension (errno = EINVAL,
 * return NULL) for inputs that would otherwise be undefined behavior. Valid
 * inputs are passed straight through to the host backend unchanged.
 */
#define _DEFAULT_SOURCE
#include "../../log/log.h"
#include <errno.h>
#include <time.h>

char *cplus_asctime(const struct tm *tm) {
    cplus_libc_log("asctime", "enter");
    if (!tm) { errno = EINVAL; return NULL; }
    return asctime(tm);
}
char *cplus_ctime(const time_t *timep) {
    cplus_libc_log("ctime", "enter");
    if (!timep) { errno = EINVAL; return NULL; }
    return ctime(timep);
}
