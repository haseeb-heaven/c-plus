/* Platform-backed string extras (C+ libc slice), batch 6. Delegate to the
 * installed system libc (the BSD strlcpy/strlcat/strnstr are declared by the
 * macOS <string.h>); logged per C+ convention.
 * NOTE: strnlen/stpcpy/memmem/strcasestr already exist in this slice
 * (extra.c / musl_extra.c) and are therefore intentionally not re-defined. */
#define _DEFAULT_SOURCE
#include "../../log/log.h"
#include <string.h>

size_t cplus_strlcpy(char *dst, const char *src, size_t size) {
    cplus_libc_log("strlcpy", "enter");
    return strlcpy(dst, src, size);
}
size_t cplus_strlcat(char *dst, const char *src, size_t size) {
    cplus_libc_log("strlcat", "enter");
    return strlcat(dst, src, size);
}
char *cplus_strnstr(const char *haystack, const char *needle, size_t len) {
    cplus_libc_log("strnstr", "enter");
    return strnstr(haystack, needle, len);
}
