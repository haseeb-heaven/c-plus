/* Platform-backed string extras (C+ libc slice), batch 6. Delegate to the
 * installed system libc (the BSD strlcpy/strlcat/strnstr are declared by the
 * macOS <string.h>); logged per C+ convention.
 * NOTE: strnlen/stpcpy/memmem/strcasestr already exist in this slice
 * (extra.c / musl_extra.c) and are therefore intentionally not re-defined. */
#define _DEFAULT_SOURCE
#include "../../log/log.h"
#include <stddef.h>
#include <string.h>

size_t cplus_strlcpy(char *dst, const char *src, size_t size) {
    size_t slen;
    cplus_libc_log("strlcpy", "enter");
    if (!src) return 0;
    slen = strlen(src);
    if (dst && size > 0) {
        size_t n = slen < size - 1 ? slen : size - 1;
        if (n > 0) memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return slen;
}
size_t cplus_strlcat(char *dst, const char *src, size_t size) {
    size_t dlen = 0;
    size_t slen;
    cplus_libc_log("strlcat", "enter");
    if (!src) return 0;
    slen = strlen(src);
    if (!dst) return slen;
    while (dlen < size && dst[dlen] != '\0') ++dlen;
    if (dlen == size) return size + slen;
    {
        size_t avail = size - dlen - 1;
        size_t n = slen < avail ? slen : avail;
        if (n > 0) memcpy(dst + dlen, src, n);
        dst[dlen + n] = '\0';
    }
    return dlen + slen;
}
char *cplus_strnstr(const char *haystack, const char *needle, size_t len) {
    size_t nlen;
    size_t i;
    cplus_libc_log("strnstr", "enter");
    if (!haystack || !needle) return NULL;
    if (needle[0] == '\0') return (char *)haystack;
    nlen = strlen(needle);
    if (nlen > len) return NULL;
    for (i = 0; i <= len - nlen && haystack[i] != '\0'; ++i) {
        if (memcmp(haystack + i, needle, nlen) == 0) {
            return (char *)(haystack + i);
        }
    }
    return NULL;
}
