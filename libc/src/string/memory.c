#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include "../../log/log.h"

void *cplus_memcpy(void *restrict dest, const void *restrict src, size_t n) {
    cplus_libc_log("memcpy", "enter");
    if (n == 0) return dest;
    if (!dest || !src) { errno = EINVAL; return dest; }
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dest;
}
void *cplus_memmove(void *dest, const void *src, size_t n) {
    cplus_libc_log("memmove", "enter");
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) return dest;
    if (!d || !s) { errno = EINVAL; return dest; }
    if ((uintptr_t)d < (uintptr_t)s) for (size_t i = 0; i < n; ++i) d[i] = s[i];
    else for (size_t i = n; i > 0; --i) d[i-1] = s[i-1];
    return dest;
}
void *cplus_memset(void *s, int c, size_t n) {
    cplus_libc_log("memset", "enter");
    if (n == 0) return s;
    if (!s) { errno = EINVAL; return s; }
    unsigned char *p = (unsigned char *)s;
    for (size_t i = 0; i < n; ++i) p[i] = (unsigned char)c;
    return s;
}
int cplus_memcmp(const void *s1, const void *s2, size_t n) {
    cplus_libc_log("memcmp", "enter");
    if (n == 0) return 0;
    if (!s1 || !s2) { errno = EINVAL; return (s1 == s2) ? 0 : (s1 ? 1 : -1); }
    const unsigned char *a = (const unsigned char *)s1;
    const unsigned char *b = (const unsigned char *)s2;
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    }
    return 0;
}
