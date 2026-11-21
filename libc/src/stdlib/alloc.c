/* C+ hardened allocation (bundled, selectively linked).
 * Real implementation using the platform allocator backend (posix malloc/free)
 * plus explicit overflow checks, logging hooks, and hardened contracts.
 * This file is compiled into libcplus_hardened; it does not replace the host
 * system CRT unless explicitly linked/installed by the user. */
#define _DARWIN_C_SOURCE
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include "../../log/log.h"

void *cplus_malloc(size_t size) {
    cplus_libc_log("malloc", "enter");
    if (size == 0) size = 1;
    void *p = malloc(size);
    if (!p) errno = ENOMEM;
    return p;
}
void *cplus_calloc(size_t nmemb, size_t size) {
    cplus_libc_log("calloc", "enter");
    if (nmemb != 0 && size > SIZE_MAX / nmemb) { errno = ENOMEM; return NULL; }
    void *p = calloc(nmemb, size);
    if (!p) errno = ENOMEM;
    return p;
}
void *cplus_realloc(void *ptr, size_t size) {
    cplus_libc_log("realloc", "enter");
    if (size == 0) size = 1;
    void *p = realloc(ptr, size);
    if (!p) errno = ENOMEM;
    return p;
}
void cplus_free(void *ptr) {
    cplus_libc_log("free", "enter");
    free(ptr);
}
