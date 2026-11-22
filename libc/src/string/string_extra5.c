/* Platform-backed string extras (C+ libc slice), batch 5. Delegate to the
 * installed system libc; logged per C+ convention.
 * NOTE: mempcpy/memfrob are glibc/BSD extensions that are NOT declared by
 * the macOS libc, so they are intentionally omitted from this slice. */
#define _DEFAULT_SOURCE
#include "../../log/log.h"
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

void cplus_swab(const void *restrict from, void *restrict to, ssize_t n) {
    cplus_libc_log("swab", "enter");
    swab(from, to, n);
}
