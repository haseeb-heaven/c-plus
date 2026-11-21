/*
 * musl-derived string functions (copied verbatim from musl libc, renamed to the
 * C+ cplus_ namespace). See libc/upstream/musl/PROVENANCE.md and
 * third_party attribution: musl @ 7813e9119eb5fe681b6750b8a033a0490bca0941, MIT.
 *
 * Local adaptations (only): function names cplus_*; strtok_r internal strspn/
 * strcspn use the C+ cplus_ implementations; logging added per C+ convention.
 */
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include "../../log/log.h"

#define ALIGN (sizeof(size_t)-1)
#define ONES ((size_t)-1/UCHAR_MAX)
#define HIGHS (ONES * (UCHAR_MAX/2+1))
#define HASZERO(x) ((x)-ONES & ~(x) & HIGHS)

size_t cplus_strspn(const char *s, const char *accept);
size_t cplus_strcspn(const char *s, const char *reject);

/* musl: src/string/memccpy.c */
void *cplus_memccpy(void *restrict dest, const void *restrict src, int c, size_t n)
{
    cplus_libc_log("memccpy", "enter");
    unsigned char *d = dest;
    const unsigned char *s = src;

    c = (unsigned char)c;
#ifdef __GNUC__
    {
        typedef size_t __attribute__((__may_alias__)) word;
        word *wd;
        const word *ws;
        if (((uintptr_t)s & ALIGN) == ((uintptr_t)d & ALIGN)) {
            for (; ((uintptr_t)s & ALIGN) && n && (*d=*s)!=c; n--, s++, d++);
            if ((uintptr_t)s & ALIGN) goto tail;
            {
                size_t k = ONES * c;
                wd=(void *)d; ws=(const void *)s;
                for (; n>=sizeof(size_t) && !HASZERO(*ws^k);
                       n-=sizeof(size_t), ws++, wd++) *wd = *ws;
                d=(void *)wd; s=(const void *)ws;
            }
        }
    }
#endif
    for (; n && (*d=*s)!=c; n--, s++, d++);
tail:
    if (n) return d+1;
    return 0;
}

/* musl: src/string/stpcpy.c */
char *cplus_stpcpy(char *restrict d, const char *restrict s)
{
    cplus_libc_log("stpcpy", "enter");
#ifdef __GNUC__
    {
        typedef size_t __attribute__((__may_alias__)) word;
        word *wd;
        const word *ws;
        if ((uintptr_t)s % ALIGN == (uintptr_t)d % ALIGN) {
            for (; (uintptr_t)s % ALIGN; s++, d++)
                if (!(*d=*s)) return d;
            wd=(void *)d; ws=(const void *)s;
            for (; !HASZERO(*ws); *wd++ = *ws++);
            d=(void *)wd; s=(const void *)ws;
        }
    }
#endif
    for (; (*d=*s); s++, d++);
    return d;
}

/* musl: src/string/stpncpy.c */
char *cplus_stpncpy(char *restrict d, const char *restrict s, size_t n)
{
    cplus_libc_log("stpncpy", "enter");
#ifdef __GNUC__
    {
        typedef size_t __attribute__((__may_alias__)) word;
        word *wd;
        const word *ws;
        if (((uintptr_t)s & ALIGN) == ((uintptr_t)d & ALIGN)) {
            for (; ((uintptr_t)s & ALIGN) && n && (*d=*s); n--, s++, d++);
            if (!n || !*s) goto tail;
            wd=(void *)d; ws=(const void *)s;
            for (; n>=sizeof(size_t) && !HASZERO(*ws);
                   n-=sizeof(size_t), ws++, wd++) *wd = *ws;
            d=(void *)wd; s=(const void *)ws;
        }
    }
#endif
    for (; n && (*d=*s); n--, s++, d++);
tail:
    memset(d, 0, n);
    return d;
}

/* musl: src/string/strtok_r.c (strspn/strcspn -> C+ implementations) */
char *cplus_strtok_r(char *restrict s, const char *restrict sep, char **restrict p)
{
    cplus_libc_log("strtok_r", "enter");
    if (!s && !(s = *p)) return NULL;
    s += cplus_strspn(s, sep);
    if (!*s) return *p = 0;
    *p = s + cplus_strcspn(s, sep);
    if (**p) *(*p)++ = 0;
    else *p = 0;
    return s;
}

/* musl: src/string/strsep.c */
char *cplus_strsep(char **str, const char *sep)
{
    cplus_libc_log("strsep", "enter");
    char *s = *str, *end;
    if (!s) return NULL;
    end = s + cplus_strcspn(s, sep);
    if (*end) *end++ = 0;
    else end = 0;
    *str = end;
    return s;
}
