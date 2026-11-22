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
#include <ctype.h>
#include <string.h>
#include "../../log/log.h"

#define ALIGN (sizeof(size_t)-1)
#define ONES ((size_t)-1/UCHAR_MAX)
#define HIGHS (ONES * (UCHAR_MAX/2+1))
#define HASZERO(x) ((x)-ONES & ~(x) & HIGHS)

size_t cplus_strspn(const char *s, const char *accept);
void *cplus_memchr(const void *s, int c, size_t n);
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

/* musl: src/string/memmem.c (memchr -> cplus_memchr) */
static char *twobyte_memmem(const unsigned char *h, size_t k, const unsigned char *n)
{
    uint16_t nw = n[0]<<8 | n[1], hw = h[0]<<8 | h[1];
    for (h+=2, k-=2; k; k--, hw = hw<<8 | *h++)
        if (hw == nw) return (char *)h-2;
    return hw == nw ? (char *)h-2 : 0;
}
static char *threebyte_memmem(const unsigned char *h, size_t k, const unsigned char *n)
{
    uint32_t nw = (uint32_t)n[0]<<24 | n[1]<<16 | n[2]<<8;
    uint32_t hw = (uint32_t)h[0]<<24 | h[1]<<16 | h[2]<<8;
    for (h+=3, k-=3; k; k--, hw = (hw|*h++)<<8)
        if (hw == nw) return (char *)h-3;
    return hw == nw ? (char *)h-3 : 0;
}
static char *fourbyte_memmem(const unsigned char *h, size_t k, const unsigned char *n)
{
    uint32_t nw = (uint32_t)n[0]<<24 | n[1]<<16 | n[2]<<8 | n[3];
    uint32_t hw = (uint32_t)h[0]<<24 | h[1]<<16 | h[2]<<8 | h[3];
    for (h+=4, k-=4; k; k--, hw = hw<<8 | *h++)
        if (hw == nw) return (char *)h-4;
    return hw == nw ? (char *)h-4 : 0;
}
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define BITOP(a,b,op) \
 ((a)[(size_t)(b)/(8*sizeof *(a))] op (size_t)1<<((size_t)(b)%(8*sizeof *(a))))
static char *twoway_memmem(const unsigned char *h, const unsigned char *z, const unsigned char *n, size_t l)
{
    size_t i, ip, jp, k, p, ms, p0, mem, mem0;
    size_t byteset[32 / sizeof(size_t)] = { 0 };
    size_t shift[256];
    for (i=0; i<l; i++)
        BITOP(byteset, n[i], |=), shift[n[i]] = i+1;
    ip = -1; jp = 0; k = p = 1;
    while (jp+k<l) {
        if (n[ip+k] == n[jp+k]) { if (k == p) { jp += p; k = 1; } else k++; }
        else if (n[ip+k] > n[jp+k]) { jp += k; k = 1; p = jp - ip; }
        else { ip = jp++; k = p = 1; }
    }
    ms = ip; p0 = p;
    ip = -1; jp = 0; k = p = 1;
    while (jp+k<l) {
        if (n[ip+k] == n[jp+k]) { if (k == p) { jp += p; k = 1; } else k++; }
        else if (n[ip+k] < n[jp+k]) { jp += k; k = 1; p = jp - ip; }
        else { ip = jp++; k = p = 1; }
    }
    if (ip+1 > ms+1) ms = ip; else p = p0;
    if (memcmp(n, n+p, ms+1)) { mem0 = 0; p = MAX(ms, l-ms-1) + 1; } else mem0 = l-p;
    mem = 0;
    for (;;) {
        if ((size_t)(z-h) < l) return 0;
        if (BITOP(byteset, h[l-1], &)) {
            k = l-shift[h[l-1]];
            if (k) { if (k < mem) k = mem; h += k; mem = 0; continue; }
        } else { h += l; mem = 0; continue; }
        for (k=MAX(ms+1,mem); k<l && n[k] == h[k]; k++);
        if (k < l) { h += k-ms; mem = 0; continue; }
        for (k=ms+1; k>mem && n[k-1] == h[k-1]; k--);
        if (k <= mem) return (char *)h;
        h += p; mem = mem0;
    }
}
void *cplus_memmem(const void *h0, size_t k, const void *n0, size_t l)
{
    cplus_libc_log("memmem", "enter");
    const unsigned char *h = h0, *n = n0;
    if (!l) return (void *)h;
    if (k<l) return 0;
    h = cplus_memchr(h0, *n, k);
    if (!h || l==1) return (void *)h;
    k -= h - (const unsigned char *)h0;
    if (k<l) return 0;
    if (l==2) return twobyte_memmem(h, k, n);
    if (l==3) return threebyte_memmem(h, k, n);
    if (l==4) return fourbyte_memmem(h, k, n);
    return twoway_memmem(h, h+k, n, l);
}

/* musl: src/string/strverscmp.c (isdigit is libc; no logging change) */
int cplus_strverscmp(const char *l0, const char *r0)
{
    cplus_libc_log("strverscmp", "enter");
    const unsigned char *l = (const void *)l0;
    const unsigned char *r = (const void *)r0;
    size_t i, dp, j;
    int z = 1;
    for (dp=i=0; l[i]==r[i]; i++) {
        int c = l[i];
        if (!c) return 0;
        if (!isdigit(c)) dp=i+1, z=1;
        else if (c!='0') z=0;
    }
    if (l[dp]-'1'<9U && r[dp]-'1'<9U) {
        for (j=i; isdigit(l[j]); j++)
            if (!isdigit(r[j])) return 1;
        if (isdigit(r[j])) return -1;
    } else if (z && dp<i && (isdigit(l[i]) || isdigit(r[i]))) {
        return (unsigned char)(l[i]-'0') - (unsigned char)(r[i]-'0');
    }
    return l[i] - r[i];
}

/* musl: src/string/strcasestr.c (strncasecmp -> cplus_strncasecmp) */
int cplus_strncasecmp(const char *a, const char *b, size_t n);
char *cplus_strcasestr(const char *h, const char *n)
{
    cplus_libc_log("strcasestr", "enter");
    size_t l = strlen(n);
    if (!l) return (char *)h;
    for (; *h; h++) if (!cplus_strncasecmp(h, n, l)) return (char *)h;
    return 0;
}

/* musl: src/string/strchrnul.c (renamed; weak_alias dropped) */
char *cplus_strchrnul(const char *s, int c)
{
    cplus_libc_log("strchrnul", "enter");
    c = (unsigned char)c;
    if (!c) return (char *)s + strlen(s);
#ifdef __GNUC__
    {
        typedef size_t __attribute__((__may_alias__)) word;
        const word *w;
        for (; (uintptr_t)s % ALIGN; s++)
            if (!*s || *(unsigned char *)s == c) return (char *)s;
        size_t k = ONES * c;
        for (w = (void *)s; !HASZERO(*w) && !HASZERO(*w^k); w++);
        s = (void *)w;
    }
#endif
    for (; *s && *(unsigned char *)s != c; s++);
    return (char *)s;
}
