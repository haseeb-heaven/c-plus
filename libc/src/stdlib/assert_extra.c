/* Platform-backed errno helpers (C+ libc slice), batch 7.
 *
 * cplus_perror already lives in src/stdio/stdio2.c (stdio slice), so only the
 * int-return XSI/POSIX strerror_r() form is added here. Host probe (Apple
 * clang 16 / Darwin, and glibc): `int strerror_r(int, char *, size_t)` is
 * present. The GNU char*-return form is not used, so this wrapper is portable
 * across the int-form hosts. */
#define _DEFAULT_SOURCE
#include "../../log/log.h"
#include <string.h>

int cplus_strerror_r(int errnum, char *buf, size_t buflen) {
    cplus_libc_log("strerror_r", "enter");
    return strerror_r(errnum, buf, buflen);
}
