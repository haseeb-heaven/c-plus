#include <stddef.h>
#include <stdint.h>
#include "../../log/log.h"

void *cplus_memcpy(void *restrict dest, const void *restrict src, size_t n) {
    cplus_libc_log("memcpy", "enter");
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
    if (d < s) for (size_t i = 0; i < n; ++i) d[i] = s[i];
    else for (size_t i = n; i > 0; --i) d[i-1] = s[i-1];
    return dest;
}
void *cplus_memset(void *s, int c, size_t n) {
    cplus_libc_log("memset", "enter");
    unsigned char *p = (unsigned char *)s;
    for (size_t i = 0; i < n; ++i) p[i] = (unsigned char)c;
    return s;
}
int cplus_memcmp(const void *s1, const void *s2, size_t n) {
    cplus_libc_log("memcmp", "enter");
    const unsigned char *a = (const unsigned char *)s1;
    const unsigned char *b = (const unsigned char *)s2;
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    }
    return 0;
}
