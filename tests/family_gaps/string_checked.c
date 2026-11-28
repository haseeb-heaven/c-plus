/* string_checked.c -- checked-coverage evidence for the string.h family.
 *
 * This translation unit is built twice, like tests/methods/hundred_string.c:
 *   1. SAFECRT layer  : stdlib/safe (safe_c.h) + hardened libc.  Ordinary C
 *      names (memcpy/strcpy/...) are routed by safe_c.h to the checked
 *      cplus_*_s_auto wrappers; checked _s APIs are exercised directly too.
 *   2. PLAIN libc layer: hardened libc only.  The same operations are routed
 *      through cplus_*_bounded adapters that carry an explicit proven object
 *      size, so the two layers run the identical scenario set.
 * Both builds are also run under UBSan (-fsanitize=undefined).
 *
 * Output contract:
 *   string_checked: <N>/<N> checks ok
 * Any violated contract additionally prints a FINDING line; main returns 0 so
 * a harness can run every layer/sanitizer combination and compare counts.
 *
 * Safety: no case performs genuine UB. NULL is handed only to wrappers that
 * guard it; overlap is exercised only through memmove/checked APIs; every
 * bounded write uses a proven stack/array capacity; unterminated data is only
 * read through an explicit bound. Canaries surround each destination buffer
 * so a refused write is proven not to have touched adjacent memory.
 */
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif
#include "cplus_libc.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__has_include)
#  if __has_include(<safe_c.h>)
#    define SC_SAFECRT 1
#  endif
#endif

#ifdef SC_SAFECRT
#  define CPLUS_SAFE_NO_UNPREFIXED_MACROS 1
#  include "safe_c.h"          /* ordinary C names -> checked _s_auto wrappers */
#  include "safe_string.h"
#  include "safe_memory.h"
#  include "safe_log.h"
#  include "safe_runtime.h"
#endif

/* ---------- reporting ---------- */
static int sc_total = 0;
static int sc_ok = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        ++sc_total;                                                            \
        if (cond) {                                                            \
            ++sc_ok;                                                           \
        } else {                                                               \
            printf("FINDING: %s\n", (msg));                                    \
        }                                                                      \
    } while (0)

/* ---------- layer-neutral byte/string helpers (no raw UB) ---------- */
static int sc_bytes_eq(const void *a, const void *b, size_t n) {
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    size_t i;
    for (i = 0; i < n; ++i)
        if (x[i] != y[i]) return 0;
    return 1;
}
static int sc_streq(const char *a, const char *b) {
    if (!a || !b) return a == b;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void sc_puts(char *d, const char *s) {
    while (*s) *d++ = *s++;
    *d = '\0';
}

/* ---------- canary-guarded destination ---------- */
#define SC_CANARY ((char)0xA5u)
#define SC_PRE 16u
#define SC_POST 16u
#define SC_BUF 64u
typedef struct {
    char pre[SC_PRE];
    char buf[SC_BUF];
    char post[SC_POST];
} sc_guard;
static void sc_guard_init(sc_guard *g) {
    size_t i;
    for (i = 0; i < sizeof(*g); ++i) ((char *)g)[i] = SC_CANARY;
}
static int sc_guard_ok(const sc_guard *g) {
    size_t i;
    for (i = 0; i < SC_PRE; ++i)
        if (g->pre[i] != SC_CANARY) return 0;
    for (i = 0; i < SC_POST; ++i)
        if (g->post[i] != SC_CANARY) return 0;
    return 1;
}

/* A long source used to force bounded-write refusals (within its own bounds). */
static char sc_long[SC_BUF + 16];
static void sc_init_long(void) {
    size_t i;
    for (i = 0; i < SC_BUF + 15; ++i) sc_long[i] = 'x';
    sc_long[SC_BUF + 15] = '\0';
}

/* ---------- layer adapters for mutating bounded operations ---------- */
#ifdef SC_SAFECRT
/* safe_c.h routes ordinary memcpy/memmove/memset to the cplus_*_s_auto
 * wrappers but casts their result to void. Call the very same _s_auto entry
 * points directly here so the refused-vs-succeeded return value is observable
 * (the task calls out checked _s_auto variants explicitly).  For the string
 * mutators and memcmp the ordinary names keep their pointer/int result. */
#define SC_MEMCPY(d, s, n)   cplus_memcpy_s_auto((d), (s), (n), CPLUS_SAFE_OBJ_SIZE(d))
#define SC_MEMMOVE(d, s, n)  cplus_memmove_s_auto((d), (s), (n), CPLUS_SAFE_OBJ_SIZE(d))
#define SC_MEMSET(d, v, n)   cplus_memset_s_auto((d), (v), (n), CPLUS_SAFE_OBJ_SIZE(d))
#define SC_MEMCMP(a, b, n)   memcmp((a), (b), (n))
#define SC_STRCPY(d, s)      strcpy((d), (s))
#define SC_STRNCPY(d, s, n)  strncpy((d), (s), (n))
#define SC_STRCAT(d, s)      strcat((d), (s))
#define SC_STRNCAT(d, s, n)  strncat((d), (s), (n))
static void sc_log(int on) {
    cplus_safe_log_set_level(on ? CPLUS_SAFE_LOG_TRACE : CPLUS_SAFE_LOG_OFF);
}
#else
#define SC_KNOWN(p)          __builtin_object_size((p), 1)
#define SC_MEMCPY(d, s, n)   cplus_memcpy_bounded((d), (s), (n), SC_KNOWN(d))
#define SC_MEMMOVE(d, s, n)  cplus_memmove_bounded((d), (s), (n), SC_KNOWN(d))
#define SC_MEMSET(d, v, n)   cplus_memset_bounded((d), (v), (n), SC_KNOWN(d))
#define SC_MEMCMP(a, b, n)   cplus_memcmp_bounded((a), (b), (n), SC_KNOWN(a), SC_KNOWN(b))
#define SC_STRCPY(d, s)      cplus_strcpy_bounded((d), (s), SC_KNOWN(d))
#define SC_STRNCPY(d, s, n)  cplus_strncpy_bounded((d), (s), (n), SC_KNOWN(d))
#define SC_STRCAT(d, s)      cplus_strcat_bounded((d), (s), SC_KNOWN(d))
#define SC_STRNCAT(d, s, n)  cplus_strncat_bounded((d), (s), (n), SC_KNOWN(d))
static void sc_log(int on) { cplus_libc_log_set_enabled(on); }
#endif

/* ---------- checked `_s` adapters (real SafeCRT, or defined shim) ---------- */
#define SC_ERR 1 /* any non-zero constraint-code failure */
#ifdef SC_SAFECRT
static int sc_strcpy_s(char *d, size_t dmax, const char *s) {
    return (int)cplus_strcpy_s(d, (rsize_t)dmax, s);
}
static int sc_strncpy_s(char *d, size_t dmax, const char *s, size_t n) {
    return (int)cplus_strncpy_s(d, (rsize_t)dmax, s, (rsize_t)n);
}
static int sc_strcat_s(char *d, size_t dmax, const char *s) {
    return (int)cplus_strcat_s(d, (rsize_t)dmax, s);
}
static int sc_memcpy_s(void *d, size_t dmax, const void *s, size_t n) {
    return (int)cplus_memcpy_s(d, (rsize_t)dmax, s, (rsize_t)n);
}
static size_t sc_strnlen_s(const char *s, size_t smax) {
    return (size_t)cplus_strnlen_s(s, (rsize_t)smax);
}
static int sc_strcmp_s(const char *a, size_t dmax, const char *b, int *ind) {
    return (int)cplus_strcmp_s(a, (rsize_t)dmax, b, ind);
}
static int sc_strerror_s(char *d, size_t dmax, int e) {
    return (int)cplus_strerror_s(d, (rsize_t)dmax, e);
}
#else
static int sc_overlap(const char *a, size_t alen, const char *b, size_t blen) {
    uintptr_t aa = (uintptr_t)a, bb = (uintptr_t)b;
    if (aa <= bb) return bb < aa + alen;
    return aa < bb + blen;
}
static int sc_strcpy_s(char *d, size_t dmax, const char *s) {
    size_t need;
    if (!d || !s || dmax == 0) return SC_ERR;
    if (d == s) return 0; /* safeclib defines exact self-copy as EOK */
    need = cplus_strlen(s) + 1u;
    if (sc_overlap(d, need, s, need)) return SC_ERR;
    if (need > dmax) return SC_ERR;
    sc_puts(d, s);
    return 0;
}
static int sc_strncpy_s(char *d, size_t dmax, const char *s, size_t n) {
    size_t i;
    if (!d || !s || dmax == 0) return SC_ERR;
    if (sc_overlap(d, dmax < n ? dmax : n, s, n)) return SC_ERR;
    if (n == 0) { d[0] = '\0'; return 0; }
    if (n >= dmax) return SC_ERR;
    for (i = 0; i < n && s[i]; ++i) d[i] = s[i];
    d[i] = '\0';
    return 0;
}
static int sc_strcat_s(char *d, size_t dmax, const char *s) {
    size_t dl, sl, i;
    if (!d || !s || dmax == 0) return SC_ERR;
    dl = cplus_strlen(d); sl = cplus_strlen(s);
    if (sc_overlap(d, dl + sl + 1u, s, sl + 1u)) return SC_ERR;
    if (dl > dmax || sl > dmax - dl - 1u) return SC_ERR;
    for (i = 0; i < sl; ++i) d[dl + i] = s[i];
    d[dl + sl] = '\0';
    return 0;
}
static int sc_memcpy_s(void *d, size_t dmax, const void *s, size_t n) {
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    size_t i;
    if (!d || !s || dmax == 0 || n > dmax) return SC_ERR;
    if (sc_overlap((const char *)d, n, (const char *)s, n)) return SC_ERR;
    for (i = 0; i < n; ++i) dd[i] = ss[i];
    return 0;
}
static size_t sc_strnlen_s(const char *s, size_t smax) {
    if (!s || smax == 0 || smax > ((size_t)-1 >> 1)) return 0;
    return cplus_strnlen(s, smax);
}
static int sc_strcmp_s(const char *a, size_t dmax, const char *b, int *ind) {
    if (!a || !b || !ind || dmax == 0) { if (ind) *ind = 0; return SC_ERR; }
    *ind = cplus_strncmp(a, b, dmax);
    return 0;
}
static int sc_strerror_s(char *d, size_t dmax, int e) {
    if (!d || dmax == 0) return SC_ERR;
    return cplus_strerror_r(e, d, dmax);
}
#endif

/* =========================== memcpy =========================== */
static void sc_t_memcpy(void) {
    sc_guard g;
    unsigned char src[SC_BUF + 8];
    size_t i;
    for (i = 0; i < sizeof src; ++i) src[i] = (unsigned char)(i * 7u + 1u);

    sc_guard_init(&g);
    CHECK(SC_MEMCPY(g.buf, src, 16) == g.buf, "memcpy: normal return");
    CHECK(sc_bytes_eq(g.buf, src, 16) && sc_guard_ok(&g),
          "memcpy: normal content/canary");

    sc_guard_init(&g);
    CHECK(SC_MEMCPY(g.buf, src, SC_BUF) == g.buf, "memcpy: exact-size return");
    CHECK(sc_bytes_eq(g.buf, src, SC_BUF) && sc_guard_ok(&g),
          "memcpy: exact-size content/canary");

    sc_guard_init(&g);
    CHECK(SC_MEMCPY(g.buf, src, 0) == g.buf && sc_guard_ok(&g),
          "memcpy: zero-length boundary");

    { char *nd = NULL; const void *ns = src;
      CHECK(SC_MEMCPY(nd, ns, 4) == NULL, "memcpy: NULL dest refused"); }
    sc_guard_init(&g);
    CHECK(SC_MEMCPY(g.buf, src, SC_BUF + 1) == NULL, "memcpy: overflow refused");
    CHECK(sc_guard_ok(&g), "memcpy: overflow canary intact");
}

/* =========================== memmove =========================== */
static void sc_t_memmove(void) {
    sc_guard g;
    unsigned char src[SC_BUF + 8];
    size_t i;
    for (i = 0; i < sizeof src; ++i) src[i] = (unsigned char)(i * 3u + 2u);

    sc_guard_init(&g);
    CHECK(SC_MEMMOVE(g.buf, src, 20) == g.buf, "memmove: normal return");
    CHECK(sc_bytes_eq(g.buf, src, 20) && sc_guard_ok(&g),
          "memmove: normal content/canary");

    sc_guard_init(&g);
    sc_puts((char *)g.buf, "abcdef");
    CHECK(SC_MEMMOVE(g.buf + 2, g.buf, 7) == g.buf + 2, "memmove: overlap return");
    CHECK(sc_streq((char *)g.buf, "ababcdef") && sc_guard_ok(&g),
          "memmove: overlapping copy defined");

    sc_guard_init(&g);
    CHECK(SC_MEMMOVE(g.buf, src, 0) == g.buf && sc_guard_ok(&g),
          "memmove: zero-length boundary");
    sc_guard_init(&g);
    CHECK(SC_MEMMOVE(g.buf, src, SC_BUF + 1) == NULL, "memmove: overflow refused");
    CHECK(sc_guard_ok(&g), "memmove: overflow canary intact");
}

/* =========================== memset =========================== */
static void sc_t_memset(void) {
    sc_guard g;
    sc_guard_init(&g);
    CHECK(SC_MEMSET(g.buf, 0x41, 16) == g.buf, "memset: normal return");
    CHECK(g.buf[0] == 0x41 && g.buf[15] == 0x41 && g.buf[16] == SC_CANARY &&
          sc_guard_ok(&g), "memset: fill boundary + canary");

    sc_guard_init(&g);
    CHECK(SC_MEMSET(g.buf, 0x41, SC_BUF) == g.buf && g.buf[SC_BUF - 1] == 0x41 &&
          sc_guard_ok(&g), "memset: exact-size fill");

    sc_guard_init(&g);
    CHECK(SC_MEMSET(g.buf, 0x41, 0) == g.buf && sc_guard_ok(&g),
          "memset: zero-length boundary");

    { char *nd = NULL;
      CHECK(SC_MEMSET(nd, 0x41, 4) == NULL, "memset: NULL dest refused"); }
    sc_guard_init(&g);
    CHECK(SC_MEMSET(g.buf, 0x41, SC_BUF + 1) == NULL, "memset: overflow refused");
    CHECK(sc_guard_ok(&g), "memset: overflow canary intact");
}

/* =========================== memcmp =========================== */
static void sc_t_memcmp(void) {
    sc_guard g;
    CHECK(SC_MEMCMP("abc", "abc", 3) == 0, "memcmp: equal");
    CHECK(SC_MEMCMP("abc", "abd", 3) < 0, "memcmp: less");
    CHECK(SC_MEMCMP("abd", "abc", 3) > 0, "memcmp: greater");
    CHECK(SC_MEMCMP("abcd", "abce", 2) == 0, "memcmp: equal within bound");
    CHECK(SC_MEMCMP("abc", "xyz", 0) == 0, "memcmp: zero-length boundary");
    sc_guard_init(&g);
    /* Refusal result differs by layer (SafeCRT -1, plain libc 0); both are
     * non-positive and, crucially, neither performs the out-of-bounds read. */
    CHECK(SC_MEMCMP(g.buf, "abc", SC_BUF + 1) <= 0, "memcmp: overflow refused");
    CHECK(sc_guard_ok(&g), "memcmp: overflow did not touch canary");
}

/* ========================== memchr/memrchr ===================== */
static void sc_t_memchr(void) {
    static const unsigned char s[8] = { 'a','b','c','b','e','f','g','h' };
    CHECK(cplus_memchr(s, 'c', 8) == s + 2, "memchr: found");
    CHECK(cplus_memchr(s, 'z', 8) == NULL, "memchr: not found");
    CHECK(cplus_memchr(s, 'h', 8) == s + 7, "memchr: boundary last byte");
    CHECK(cplus_memchr(s, 'a', 0) == NULL, "memchr: zero-length boundary");
}
static void sc_t_memrchr(void) {
    static const unsigned char s[8] = { 'a','b','c','b','e','f','g','h' };
    CHECK(cplus_memrchr(s, 'b', 8) == s + 3, "memrchr: found last");
    CHECK(cplus_memrchr(s, 'z', 8) == NULL, "memrchr: not found");
    CHECK(cplus_memrchr(s, 'a', 0) == NULL, "memrchr: zero-length boundary");
}

/* ========================= strlen/strnlen ====================== */
static void sc_t_strlen(void) {
    CHECK(cplus_strlen("hello") == 5, "strlen: normal");
    CHECK(cplus_strlen("") == 0, "strlen: empty boundary");
    CHECK(cplus_strlen("a\0bcd") == 1, "strlen: embedded NUL");
    CHECK(cplus_strlen("\xC3\xA9\x80") == 3, "strlen: high-bit bytes");
}
static void sc_t_strnlen(void) {
    static const char unterminated[4] = { 'w','x','y','z' };
    CHECK(cplus_strnlen("abcdef", 3) == 3, "strnlen: bound below length");
    CHECK(cplus_strnlen("abc", 10) == 3, "strnlen: bound above length");
    CHECK(cplus_strnlen(unterminated, 4) == 4, "strnlen: explicit-bound read");
    CHECK(cplus_strnlen("abc", 0) == 0, "strnlen: zero bound");
}

/* ====================== strcpy/strncpy ========================= */
static void sc_t_strcpy(void) {
    sc_guard g;
    sc_guard_init(&g);
    CHECK(SC_STRCPY(g.buf, "abc") == g.buf && sc_streq((char *)g.buf, "abc") &&
          sc_guard_ok(&g), "strcpy: normal");
    sc_guard_init(&g);
    CHECK(SC_STRCPY(g.buf, "") == g.buf && g.buf[0] == '\0' && sc_guard_ok(&g),
          "strcpy: empty source boundary");
    sc_guard_init(&g);
    CHECK(SC_STRCPY(g.buf, "a\0bc") == g.buf && sc_bytes_eq(g.buf, "a\0", 2),
          "strcpy: embedded NUL");
    { const char *ns = NULL;
      CHECK(SC_STRCPY(g.buf, ns) == NULL, "strcpy: NULL source refused"); }
    sc_guard_init(&g);
    CHECK(SC_STRCPY(g.buf, sc_long) == NULL, "strcpy: overflow refused");
    CHECK(sc_guard_ok(&g), "strcpy: overflow canary intact");
}
static void sc_t_strncpy(void) {
    sc_guard g;
    sc_guard_init(&g);
    CHECK(SC_STRNCPY(g.buf, "abcdef", 3) == g.buf &&
          sc_bytes_eq(g.buf, "abc", 3), "strncpy: n below length");
    sc_guard_init(&g);
    CHECK(SC_STRNCPY(g.buf, "ab", 8) == g.buf && sc_streq((char *)g.buf, "ab"),
          "strncpy: n above length");
    sc_guard_init(&g);
    g.buf[0] = 'Q';
    CHECK(SC_STRNCPY(g.buf, "xyz", 0) == g.buf && g.buf[0] == 'Q',
          "strncpy: zero-length boundary");
    sc_guard_init(&g);
    CHECK(SC_STRNCPY(g.buf, sc_long, SC_BUF + 1) == NULL,
          "strncpy: overflow refused");
    CHECK(sc_guard_ok(&g), "strncpy: overflow canary intact");
}

/* ======================== strcat/strncat ======================= */
static void sc_t_strcat(void) {
    sc_guard g;
    sc_guard_init(&g); sc_puts((char *)g.buf, "ab");
    CHECK(SC_STRCAT(g.buf, "cde") == g.buf && sc_streq((char *)g.buf, "abcde") &&
          sc_guard_ok(&g), "strcat: normal");
    sc_guard_init(&g); sc_puts((char *)g.buf, "ab");
    CHECK(SC_STRCAT(g.buf, "") == g.buf && sc_streq((char *)g.buf, "ab"),
          "strcat: empty source boundary");
    sc_guard_init(&g); sc_puts((char *)g.buf, "ab");
    CHECK(SC_STRCAT(g.buf, "x\0y") == g.buf && sc_bytes_eq(g.buf, "abx\0", 4),
          "strcat: embedded NUL");
    sc_guard_init(&g); sc_puts((char *)g.buf, "ab");
    CHECK(SC_STRCAT(g.buf, sc_long) == NULL, "strcat: overflow refused");
    CHECK(sc_guard_ok(&g), "strcat: overflow canary intact");
}
static void sc_t_strncat(void) {
    sc_guard g;
    sc_guard_init(&g); sc_puts((char *)g.buf, "ab");
    CHECK(SC_STRNCAT(g.buf, "cdefgh", 3) == g.buf &&
          sc_streq((char *)g.buf, "abcde"), "strncat: n below length");
    sc_guard_init(&g); sc_puts((char *)g.buf, "ab");
    CHECK(SC_STRNCAT(g.buf, "cdef", 0) == g.buf &&
          sc_streq((char *)g.buf, "ab"), "strncat: zero-length boundary");
    sc_guard_init(&g); sc_puts((char *)g.buf, "ab");
    CHECK(SC_STRNCAT(g.buf, "cde", 9) == g.buf &&
          sc_streq((char *)g.buf, "abcde"), "strncat: n above length");
    sc_guard_init(&g); sc_puts((char *)g.buf, "ab");
    CHECK(SC_STRNCAT(g.buf, sc_long, SC_BUF) == NULL, "strncat: overflow refused");
    CHECK(sc_guard_ok(&g), "strncat: overflow canary intact");
}

/* ======================= strcmp/strncmp ======================== */
static void sc_t_strcmp(void) {
    CHECK(cplus_strcmp("abc", "abc") == 0, "strcmp: equal");
    CHECK(cplus_strcmp("abc", "abd") < 0, "strcmp: less");
    CHECK(cplus_strcmp("abd", "abc") > 0, "strcmp: greater");
    CHECK(cplus_strcmp("ab\0z", "ab") == 0, "strcmp: embedded NUL");
}
static void sc_t_strncmp(void) {
    CHECK(cplus_strncmp("abcd", "abce", 3) == 0, "strncmp: equal within n");
    CHECK(cplus_strncmp("abcd", "abce", 4) < 0, "strncmp: differ at nth");
    CHECK(cplus_strncmp("abcd", "abce", 2) == 0, "strncmp: differ beyond n");
    CHECK(cplus_strncmp("abc", "xyz", 0) == 0, "strncmp: zero-length boundary");
}

/* ==================== strchr/strrchr/strstr ==================== */
static void sc_t_strchr(void) {
    const char s[] = "hello";
    const char hb[] = { (char)0xC3, (char)0xA9, '\0' };
    CHECK(cplus_strchr(s, 'l') == s + 2, "strchr: found");
    CHECK(cplus_strchr(s, 'z') == NULL, "strchr: not found");
    CHECK(cplus_strchr(s, '\0') == s + 5, "strchr: search NUL");
    CHECK(cplus_strchr(hb, (unsigned char)0xA9) == hb + 1, "strchr: high-bit byte");
}
static void sc_t_strrchr(void) {
    const char s[] = "a/b/c";
    CHECK(cplus_strrchr(s, '/') == s + 3, "strrchr: found last");
    CHECK(cplus_strrchr(s, '!') == NULL, "strrchr: not found");
    CHECK(cplus_strrchr("abc", '\0') == (const char *)"abc" + 3,
          "strrchr: search NUL");
}
static void sc_t_strstr(void) {
    const char s[] = "abcde";
    CHECK(cplus_strstr(s, "cd") == s + 2, "strstr: found");
    CHECK(cplus_strstr(s, "xy") == NULL, "strstr: not found");
    CHECK(cplus_strstr(s, "") == s, "strstr: empty needle boundary");
}

/* ======================= strdup/strndup ======================== */
static void sc_t_strdup(void) {
    char *p = cplus_strdup("duplicate-me");
    CHECK(p != NULL && sc_streq(p, "duplicate-me"), "strdup: normal");
    if (p) cplus_free(p);
    p = cplus_strdup("");
    CHECK(p != NULL && p[0] == '\0', "strdup: empty boundary");
    if (p) cplus_free(p);
    p = cplus_strdup("embedded\0tail");
    CHECK(p != NULL && sc_bytes_eq(p, "embedded\0", 9), "strdup: embedded NUL");
    if (p) cplus_free(p);
}
static void sc_t_strndup(void) {
    char *p = cplus_strndup("abcdef", 3);
    CHECK(p != NULL && sc_streq(p, "abc"), "strndup: n below length");
    if (p) cplus_free(p);
    p = cplus_strndup("abc", 10);
    CHECK(p != NULL && sc_streq(p, "abc"), "strndup: n above length");
    if (p) cplus_free(p);
    p = cplus_strndup("ab\0cd", 5);
    CHECK(p != NULL && sc_bytes_eq(p, "ab\0", 3), "strndup: embedded NUL");
    if (p) cplus_free(p);
    p = cplus_strndup("abc", 0);
    CHECK(p != NULL && p[0] == '\0', "strndup: zero-length boundary");
    if (p) cplus_free(p);
}

/* =================== strtok/strtok_r/strsep ==================== */
static void sc_t_strtok(void) {
    char s[] = "one two three";
    char *t = cplus_strtok(s, " ");
    CHECK(t != NULL && sc_streq(t, "one"), "strtok: basic");
    { char a[] = "a,,b"; char *q; int n = 0;
      for (q = cplus_strtok(a, ","); q; q = cplus_strtok(NULL, ",")) ++n;
      CHECK(n == 2, "strtok: consecutive delimiters"); }
    { char a[] = "abc"; char *q = cplus_strtok(a, ",");
      CHECK(q != NULL && sc_streq(q, "abc"), "strtok: no delimiter"); }
}
static void sc_t_strtok_r(void) {
    char s[] = "x-y-z";
    char *save = NULL;
    char *t;
    int n = 0;
    for (t = cplus_strtok_r(s, "-", &save); t; t = cplus_strtok_r(NULL, "-", &save)) ++n;
    CHECK(n == 3, "strtok_r: multiple tokens");
    { char a[] = "abc"; char *sv = NULL; char *q = cplus_strtok_r(a, ",", &sv);
      CHECK(q != NULL && sc_streq(q, "abc"), "strtok_r: no delimiter"); }
    { char a[] = " a b "; char *sv = NULL; char *q = cplus_strtok_r(a, " ", &sv);
      CHECK(q != NULL && sc_streq(q, "a"), "strtok_r: leading delimiters skipped"); }
}
static void sc_t_strsep(void) {
    char s[] = "a:b:c";
    char *p = s;
    char *t = cplus_strsep(&p, ":");
    CHECK(t && sc_streq(t, "a") && p && sc_streq(p, "b:c"), "strsep: basic");
    { char a[] = "a::b"; char *q = a; char *r; int empty = 0;
      (void)cplus_strsep(&q, ":"); r = cplus_strsep(&q, ":");
      empty = (r && r[0] == '\0');
      CHECK(empty, "strsep: consecutive delimiters"); }
    { char a[] = "abc"; char *q = a; char *r = cplus_strsep(&q, ":");
      CHECK(r && sc_streq(r, "abc") && q == NULL, "strsep: no delimiter"); }
}

/* ================ strspn/strcspn/strpbrk ======================= */
static void sc_t_strspn(void) {
    CHECK(cplus_strspn("abc123", "abc") == 3, "strspn: basic");
    CHECK(cplus_strspn("xyz", "abc") == 0, "strspn: none match");
    CHECK(cplus_strspn("aaa", "abc") == 3, "strspn: all match");
}
static void sc_t_strcspn(void) {
    CHECK(cplus_strcspn("abc123", "123") == 3, "strcspn: basic");
    CHECK(cplus_strcspn("abc", "xyz") == 3, "strcspn: none match");
    CHECK(cplus_strcspn("abc", "a") == 0, "strcspn: first byte match");
}
static void sc_t_strpbrk(void) {
    const char s[] = "abcde";
    const char hb[] = { (char)0xC3, (char)0xA9, '\0' };
    const char set[] = { (char)0xA9, '\0' };
    CHECK(cplus_strpbrk(s, "cd") == s + 2, "strpbrk: found");
    CHECK(cplus_strpbrk("abc", "xyz") == NULL, "strpbrk: not found");
    CHECK(cplus_strpbrk(hb, set) == hb + 1, "strpbrk: high-bit byte");
}

/* ==================== strerror/strsignal ======================= */
static void sc_t_strerror_signal(void) {
    char *m = cplus_strerror(EINVAL);
    CHECK(m && *m && sc_streq(m, "Invalid argument"), "strerror: EINVAL message");
    CHECK(cplus_strerror(0) != NULL, "strerror: success code message");
    CHECK(cplus_strerror(9999) != NULL, "strerror: unknown code message");
    CHECK(cplus_strsignal(SIGSEGV) != NULL && *(cplus_strsignal(SIGSEGV)) != '\0',
          "strsignal: SIGSEGV message");
    CHECK(cplus_strsignal(0) != NULL, "strsignal: unknown signal message");
}

/* ======================= strcoll/strxfrm ======================= */
static void sc_t_strcoll_xfrm(void) {
    char d[16];
    size_t i;
    CHECK(cplus_strcoll("abc", "abc") == 0, "strcoll: equal");
    CHECK(cplus_strcoll("abc", "abd") < 0, "strcoll: less");
    CHECK(cplus_strcoll("abd", "abc") > 0, "strcoll: greater");

    for (i = 0; i < sizeof d; ++i) d[i] = 'Z';
    CHECK(cplus_strxfrm(d, "abc", sizeof d) == 3, "strxfrm: required length");
    CHECK(sc_streq(d, "abc"), "strxfrm: transformed text in C locale");
    for (i = 0; i < sizeof d; ++i) d[i] = 'Z';
    CHECK(cplus_strxfrm(d, "abcdefgh", 4) > 0, "strxfrm: small-n boundary");
}

/* ======================== stpcpy/stpncpy ======================= */
static void sc_t_stpcpy(void) {
    sc_guard g;
    sc_guard_init(&g);
    CHECK(cplus_stpcpy((char *)g.buf, "abc") == (char *)g.buf + 3 &&
          sc_bytes_eq(g.buf, "abc\0", 4), "stpcpy: returns terminator");
    sc_guard_init(&g);
    CHECK(cplus_stpcpy((char *)g.buf, "") == (char *)g.buf && g.buf[0] == '\0',
          "stpcpy: empty boundary");
    sc_guard_init(&g);
    CHECK(cplus_stpcpy((char *)g.buf, "a\0b") == (char *)g.buf + 1 &&
          sc_bytes_eq(g.buf, "a\0", 2), "stpcpy: embedded NUL");
}
static void sc_t_stpncpy(void) {
    sc_guard g;
    sc_guard_init(&g);
    CHECK(cplus_stpncpy((char *)g.buf, "ab", 5) == (char *)g.buf + 2 &&
          g.buf[2] == '\0' && g.buf[4] == '\0', "stpncpy: n above length pads NUL");
    sc_guard_init(&g);
    CHECK(cplus_stpncpy((char *)g.buf, "abc", 3) == (char *)g.buf + 3 &&
          g.buf[3] == SC_CANARY, "stpncpy: n equals length boundary");
    sc_guard_init(&g);
    CHECK(cplus_stpncpy((char *)g.buf, "abcdef", 3) == (char *)g.buf + 3 &&
          sc_bytes_eq(g.buf, "abc", 3), "stpncpy: n below length");
}

/* ====== memccpy/memmem/strcasestr/strverscmp/strchrnul ======== */
static void sc_t_memccpy(void) {
    sc_guard g;
    sc_guard_init(&g);
    CHECK(cplus_memccpy(g.buf, "abc:def", ':', 16) == g.buf + 4 &&
          sc_bytes_eq(g.buf, "abc:", 4), "memccpy: delimiter found");
    sc_guard_init(&g);
    CHECK(cplus_memccpy(g.buf, "abcdef", ':', 6) == NULL &&
          sc_bytes_eq(g.buf, "abcdef", 6), "memccpy: delimiter not found");
    sc_guard_init(&g);
    CHECK(cplus_memccpy(g.buf, "ab:cd", ':', 2) == NULL &&
          sc_guard_ok(&g), "memccpy: n boundary");
}
static void sc_t_memmem(void) {
    const char hay[] = "abcdefghi";
    CHECK(cplus_memmem(hay, 9, "def", 3) == hay + 3, "memmem: found");
    CHECK(cplus_memmem(hay, 9, "xyz", 3) == NULL, "memmem: not found");
    CHECK(cplus_memmem(hay, 9, "", 0) == hay, "memmem: empty needle boundary");
    CHECK(cplus_memmem(hay, 0, "a", 1) == NULL, "memmem: zero haystack boundary");
}
static void sc_t_strcasestr(void) {
    CHECK(cplus_strcasestr("Hello World", "world") != NULL,
          "strcasestr: case-insensitive found");
    CHECK(cplus_strcasestr("abc", "XY") == NULL, "strcasestr: not found");
    CHECK(cplus_strcasestr("abc", "abc") != NULL, "strcasestr: exact match");
    CHECK(cplus_strcasestr("abc", "") != NULL, "strcasestr: empty needle boundary");
}
static void sc_t_strverscmp(void) {
    CHECK(cplus_strverscmp("abc", "abc") == 0, "strverscmp: equal");
    CHECK(cplus_strverscmp("a2", "a10") < 0, "strverscmp: numeric order");
    CHECK(cplus_strverscmp("a10", "a2") > 0, "strverscmp: numeric order reverse");
}
static void sc_t_strchrnul(void) {
    const char s[] = "abc";
    CHECK(cplus_strchrnul(s, 'b') == s + 1, "strchrnul: found");
    CHECK(cplus_strchrnul(s, 'z') == s + 3, "strchrnul: not found returns NUL");
    CHECK(cplus_strchrnul(s, '\0') == s + 3, "strchrnul: search NUL");
}

/* ==================== checked `_s` operations ================== */
static void sc_t_checked_s(void) {
    sc_guard g;

    CHECK(sc_strcpy_s(NULL, 8, "abc") != 0, "strcpy_s: NULL dest refused");
    sc_guard_init(&g);
    CHECK(sc_strncpy_s((char *)g.buf, 4, sc_long, 6) != 0 && sc_guard_ok(&g),
          "strncpy_s: refusal preserves canary");
    sc_guard_init(&g); sc_puts((char *)g.buf, "ab");
    CHECK(sc_strcat_s((char *)g.buf, 8, "x\0y") == 0 &&
          sc_bytes_eq(g.buf, "abx\0", 4), "strcat_s: embedded NUL");
    sc_guard_init(&g);
    CHECK(sc_memcpy_s(g.buf, 4, sc_long, 6) != 0 && sc_guard_ok(&g),
          "memcpy_s: refusal preserves canary");
    CHECK(sc_strnlen_s(NULL, 10) == 0, "strnlen_s: NULL refused");
    { int ind = 99;
      CHECK(sc_strcmp_s("abc", 3, "abd", &ind) == 0 && ind < 0,
            "strcmp_s: mismatch indicator"); }
    { char b[32]; int rc = sc_strerror_s(b, sizeof b, EINVAL);
      CHECK(rc == 0 && b[0] != '\0', "strerror_s: valid message"); }
}

/* ============= cross-cutting invariants ======================= */
static void sc_logging_parity(void) {
    sc_guard ga, gb;
    char *ra, *rb;
    sc_guard_init(&ga); sc_guard_init(&gb);
    ga.buf[0] = '\0'; gb.buf[0] = '\0';
    sc_log(0);
    ra = SC_STRCAT((char *)ga.buf, sc_long);   /* too long -> refusal */
    sc_log(1);
    rb = SC_STRCAT((char *)gb.buf, sc_long);
    sc_log(0);
    CHECK((ra == NULL) == (rb == NULL) &&
          sc_bytes_eq(&ga, &gb, sizeof ga) &&
          sc_guard_ok(&ga) && sc_guard_ok(&gb),
          "logging OFF/ON parity: identical refusal and canaries");
}
static void sc_null_guarded_all(void) {
    sc_guard g;
    const char *ns = NULL;
    char *nd = NULL;
    sc_guard_init(&g); sc_puts((char *)g.buf, "d");
    CHECK(SC_STRCPY(nd, "x") == NULL &&
          SC_STRCPY(g.buf, ns) == NULL &&
          SC_STRNCPY(nd, "x", 1) == NULL &&
          SC_STRNCPY(g.buf, ns, 1) == NULL &&
          SC_STRCAT(nd, "x") == NULL &&
          SC_STRCAT(g.buf, ns) == NULL &&
          SC_STRNCAT(nd, "x", 1) == NULL &&
          SC_STRNCAT(g.buf, ns, 1) == NULL &&
          sc_streq((char *)g.buf, "d"),
          "bounded mutators: NULL guarded across all");
}
static void sc_canary_repeated_appends(void) {
    sc_guard g;
    int i;
    sc_guard_init(&g); g.buf[0] = '\0';
    for (i = 0; i < 5; ++i)
        if (!SC_STRNCAT((char *)g.buf, "ab", 2)) break;
    CHECK(sc_streq((char *)g.buf, "ababababab") && sc_guard_ok(&g),
          "strncat: canary integrity across repeated appends");
}

int main(void) {
    sc_init_long();
#ifdef SC_SAFECRT
    /* Inspect NULL/refusal return values instead of terminating on them. */
    cplus_runtime_set_violation_trap(0);
#endif

    sc_t_memcpy();       sc_t_memmove();      sc_t_memset();
    sc_t_memcmp();       sc_t_memchr();       sc_t_memrchr();
    sc_t_strlen();       sc_t_strnlen();      sc_t_strcpy();
    sc_t_strncpy();      sc_t_strcat();       sc_t_strncat();
    sc_t_strcmp();       sc_t_strncmp();      sc_t_strchr();
    sc_t_strrchr();      sc_t_strstr();       sc_t_strdup();
    sc_t_strndup();      sc_t_strtok();       sc_t_strtok_r();
    sc_t_strsep();       sc_t_strspn();       sc_t_strcspn();
    sc_t_strpbrk();      sc_t_strerror_signal();
    sc_t_strcoll_xfrm(); sc_t_stpcpy();       sc_t_stpncpy();
    sc_t_memccpy();      sc_t_memmem();       sc_t_strcasestr();
    sc_t_strverscmp();   sc_t_strchrnul();    sc_t_checked_s();
    sc_logging_parity(); sc_null_guarded_all(); sc_canary_repeated_appends();

    printf("string_checked: %d/%d checks ok\n", sc_ok, sc_total);
    return 0;
}
