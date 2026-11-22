#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include "../../log/log.h"

/* Same bound as the SafeCRT layer (stdlib/safe/safe_memory.h): lengths above
 * this are absurd for a single object and must never reach a copy/fill loop.
 * Defined locally so the plain libc layer stays independent of SafeCRT. */
#ifndef RSIZE_MAX_MEM
#define RSIZE_MAX_MEM ((size_t)(SIZE_MAX >> 1))
#endif

size_t cplus_strlen(const char *s) {
    cplus_libc_log("strlen", "enter");
    size_t n = 0; if (!s) return 0; while (s[n]) ++n; return n;
}
char *cplus_strcpy(char *restrict dest, const char *restrict src) {
    cplus_libc_log("strcpy", "enter");
    if (!dest || !src) { errno = EINVAL; return dest; }
    char *d = dest; while ((*d++ = *src++)) {} return dest;
}
char *cplus_strncpy(char *restrict dest, const char *restrict src, size_t n) {
    cplus_libc_log("strncpy", "enter");
    if (n == 0) return dest;
    if (!dest || !src) { errno = EINVAL; return dest; }
    /* Guard (#2, AUDIT_memory_string.md): the zero-fill loop below writes n
     * bytes. An unvalidated SIZE_MAX n would therefore walk far past the
     * destination. Reject lengths above RSIZE_MAX_MEM with EOVERFLOW instead
     * of passing SIZE_MAX into the loops; valid n keeps identical semantics. */
    if (n > RSIZE_MAX_MEM) { errno = EOVERFLOW; return dest; }
    size_t i = 0; for (; i < n && src[i]; ++i) dest[i] = src[i];
    for (; i < n; ++i) dest[i] = '\0';
    return dest;
}
char *cplus_strcat(char *restrict dest, const char *restrict src) {
    cplus_libc_log("strcat", "enter");
    if (!dest || !src) { errno = EINVAL; return dest; }
    char *d = dest; while (*d) ++d; while ((*d++ = *src++)) {} return dest;
}
char *cplus_strncat(char *restrict dest, const char *restrict src, size_t n) {
    cplus_libc_log("strncat", "enter");
    if (!dest || !src) { errno = EINVAL; return dest; }
    char *d = dest; while (*d) ++d; size_t i=0; for (; i<n && src[i]; ++i) d[i]=src[i]; d[i]='\0'; return dest;
}
int cplus_strcmp(const char *s1, const char *s2) {
    cplus_libc_log("strcmp", "enter");
    if (!s1 || !s2) { errno = EINVAL; return (s1 == s2) ? 0 : (s1 ? 1 : -1); }
    while (*s1 && (*s1 == *s2)) { ++s1; ++s2; }
    return (unsigned char)*s1 - (unsigned char)*s2;
}
int cplus_strncmp(const char *s1, const char *s2, size_t n) {
    cplus_libc_log("strncmp", "enter");
    if (n == 0) return 0;
    if (!s1 || !s2) { errno = EINVAL; return (s1 == s2) ? 0 : (s1 ? 1 : -1); }
    for (size_t i=0;i<n;++i) {
        unsigned char a=(unsigned char)s1[i], b=(unsigned char)s2[i];
        if (a!=b) return a-b;
        if (a==0) return 0;
    }
    return 0;
}
