/* Additional string/memory functions for C+ hardened libc.
 * Implementations are small, standard-conforming C. Reference behavior cross-checked
 * against musl (MIT, commit 7813e9119eb5fe681b6750b8a033a0490bca0941) and system libc. */
#include <stddef.h>
#include "../../log/log.h"

void *cplus_memchr(const void *s, int c, size_t n) {
    cplus_libc_log("memchr", "enter");
    const unsigned char *p = (const unsigned char *)s;
    unsigned char uc = (unsigned char)c;
    for (size_t i = 0; i < n; ++i) if (p[i] == uc) return (void *)(p + i);
    return NULL;
}
char *cplus_strchr(const char *s, int c) {
    cplus_libc_log("strchr", "enter");
    char ch = (char)c;
    for (; *s; ++s) if (*s == ch) return (char *)s;
    return ch == '\0' ? (char *)s : NULL;
}
char *cplus_strrchr(const char *s, int c) {
    cplus_libc_log("strrchr", "enter");
    char ch = (char)c;
    const char *last = NULL;
    for (;; ++s) {
        if (*s == ch) last = s;
        if (*s == '\0') break;
    }
    return (char *)last;
}
char *cplus_strstr(const char *h, const char *n) {
    cplus_libc_log("strstr", "enter");
    if (!*n) return (char *)h;
    for (; *h; ++h) {
        const char *p = h, *q = n;
        while (*p && *q && *p == *q) { ++p; ++q; }
        if (!*q) return (char *)h;
    }
    return NULL;
}
size_t cplus_strnlen(const char *s, size_t n) {
    cplus_libc_log("strnlen", "enter");
    size_t i = 0;
    if (!s) return 0;
    for (; i < n && s[i]; ++i) {}
    return i;
}
