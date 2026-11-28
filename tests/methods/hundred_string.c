/* hundred_string.c -- 100 DISTINCT runtime scenarios for string methods.
 *
 * The suite is deliberately layer-agnostic: the harness (tests/methods/
 * run_hundred.sh) builds this same translation unit twice, once against the
 * C+ SafeCRT layer (stdlib/safe, cplus_*_s / cplus_*_s_auto) and once against
 * the plain hardened libc layer (libc, cplus_*_bounded / cplus_*).  We detect
 * which layer is reachable with __has_include and route every scenario
 * through a thin adapter (hs_*), so all 100 cases run unchanged in both
 * layers and under UBSan.
 *
 * Output contract:
 *   hundred_string: case <n> <method> <scenario> <OUTCOME>
 *   ... x100
 *   hundred_string: <N>/100 scenarios ok
 * A mismatch additionally prints a FINDING line.  main always returns 0 so
 * the harness can run every layer/sanitizer combination and compare counts.
 *
 * Safety: no case performs genuine UB.  NULL is only passed to wrappers that
 * guard it; overlap/self-copy is only exercised through the checked API that
 * detects or defines it; unterminated buffers are only read with an explicit
 * bound; every bounded write uses a proven stack/heap capacity.
 */
#include "cplus_libc.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__has_include)
#  if __has_include(<safe_string.h>)
#    define HS_SAFECRT 1
#  endif
#endif

#ifdef HS_SAFECRT
#  define CPLUS_SAFE_NO_UNPREFIXED_MACROS 1
#  include "safe_string.h"
#  include "safe_alloc.h"
#  include "safe_log.h"
#  include "safe_runtime.h"
#else
/* The plain libc layer exposes the declarations through cplus_libc.h. */
#endif

#define HS_ERR 1 /* any non-zero checked-code failure */

/* ---- layer adapters: mutating operations with proven capacity ----------- */
#ifdef HS_SAFECRT
static char *hs_strcpy(char *d, size_t cap, const char *s) {
    return cplus_strcpy_s_auto(d, s, cap);
}
static char *hs_strncpy(char *d, size_t cap, const char *s, size_t n) {
    return cplus_strncpy_s_auto(d, s, n, cap);
}
static char *hs_strcat(char *d, size_t cap, const char *s) {
    return cplus_strcat_s_auto(d, s, cap);
}
static char *hs_strncat(char *d, size_t cap, const char *s, size_t n) {
    return cplus_strncat_s_auto(d, s, n, cap);
}
static void *hs_alloc(size_t n) { return malloc_s(n); }
static void *hs_realloc(void *p, size_t n) { return realloc_s(p, n); }
static void hs_free(void *p) { free_s(p); }
static size_t hs_cap(const void *p) { return cplus_safe_remaining_capacity(p); }
static void hs_log(int on) {
    cplus_safe_log_set_level(on ? CPLUS_SAFE_LOG_TRACE : CPLUS_SAFE_LOG_OFF);
}
#else
static char *hs_strcpy(char *d, size_t cap, const char *s) {
    return cplus_strcpy_bounded(d, s, cap);
}
static char *hs_strncpy(char *d, size_t cap, const char *s, size_t n) {
    return cplus_strncpy_bounded(d, s, n, cap);
}
static char *hs_strcat(char *d, size_t cap, const char *s) {
    return cplus_strcat_bounded(d, s, cap);
}
static char *hs_strncat(char *d, size_t cap, const char *s, size_t n) {
    return cplus_strncat_bounded(d, s, n, cap);
}
static void *hs_alloc(size_t n) { return cplus_malloc(n); }
static void *hs_realloc(void *p, size_t n) { return cplus_realloc(p, n); }
static void hs_free(void *p) { cplus_free(p); }
static size_t hs_cap(const void *p) { return cplus_alloc_remaining_capacity(p); }
static void hs_log(int on) { cplus_libc_log_set_enabled(on); }
#endif

/* ---- checked `_s` API: real SafeCRT in layer 1, equivalent guard shim -----
 * in layer 2.  The shim mirrors the same defined contracts (NULL refusal,
 * zero-capacity refusal, overlap refusal/self-copy success, insufficient
 * space refusal) so scenario expectations are identical in both layers. */
#ifndef HS_SAFECRT
static int hs_overlap(const char *a, size_t alen, const char *b, size_t blen) {
    uintptr_t aa = (uintptr_t)a, bb = (uintptr_t)b;
    if (aa <= bb) return bb < aa + alen;
    return aa < bb + blen;
}
#endif

static int hs_strcpy_s(char *d, size_t dmax, const char *s) {
#ifdef HS_SAFECRT
    return cplus_strcpy_s(d, dmax, s);
#else
    size_t need;
    if (!d || !s || dmax == 0) return HS_ERR;
    if (d == s) return 0; /* safeclib defines exact self-copy as EOK */
    need = cplus_strlen(s) + 1u;
    if (hs_overlap(d, need, s, need)) return HS_ERR;
    if (need > dmax) return HS_ERR;
    memcpy(d, s, need);
    return 0;
#endif
}
static int hs_strncpy_s(char *d, size_t dmax, const char *s, size_t slen) {
#ifdef HS_SAFECRT
    return cplus_strncpy_s(d, dmax, s, slen);
#else
    size_t i;
    if (!d || !s || dmax == 0) return HS_ERR;
    if (hs_overlap(d, dmax < slen ? dmax : slen, s, slen)) return HS_ERR;
    if (slen == 0) { d[0] = '\0'; return 0; }
    if (slen >= dmax) return HS_ERR;
    for (i = 0; i < slen && s[i]; ++i) d[i] = s[i];
    d[i] = '\0';
    return 0;
#endif
}
static int hs_strcat_s(char *d, size_t dmax, const char *s) {
#ifdef HS_SAFECRT
    return cplus_strcat_s(d, dmax, s);
#else
    size_t dl, sl;
    if (!d || !s || dmax == 0) return HS_ERR;
    dl = cplus_strlen(d); sl = cplus_strlen(s);
    if (hs_overlap(d, dl + sl + 1u, s, sl + 1u)) return HS_ERR;
    if (dl > dmax || sl > dmax - dl - 1u) return HS_ERR;
    memcpy(d + dl, s, sl + 1u);
    return 0;
#endif
}
static size_t hs_strnlen_s(const char *s, size_t smax) {
#ifdef HS_SAFECRT
    return cplus_strnlen_s(s, smax);
#else
    if (!s || smax == 0 || smax > ((size_t)-1 >> 1)) return 0;
    return cplus_strnlen(s, smax);
#endif
}
static int hs_strcmp_s(const char *a, size_t dmax, const char *b, int *ind) {
#ifdef HS_SAFECRT
    return cplus_strcmp_s(a, dmax, b, ind);
#else
    if (!a || !b || !ind || dmax == 0) { if (ind) *ind = 0; return HS_ERR; }
    *ind = cplus_strncmp(a, b, dmax);
    return 0;
#endif
}
static char *hs_strtok_s(char *d, size_t *dmax, const char *delim, char **ptr) {
#ifdef HS_SAFECRT
    return cplus_strtok_s(d, dmax, delim, ptr);
#else
    (void)dmax;
    return cplus_strtok_r(d, delim, ptr);
#endif
}

/* ---- reporting / canary helpers ---------------------------------------- */
#define HS_CANARY 0x7Eu
static void hs_fill(unsigned char *p, size_t n) { memset(p, HS_CANARY, n); }
static int hs_canary(const unsigned char *p, size_t a, size_t b) {
    size_t i;
    for (i = a; i < b; ++i) if (p[i] != HS_CANARY) return 0;
    return 1;
}
static int g_ok;
static int g_n;
static void hs_emit(const char *method, const char *scenario, int pass) {
    ++g_n;
    printf("hundred_string: case %d %s %s %s\n", g_n, method, scenario,
           pass ? "ok" : "MISMATCH");
    if (!pass)
        printf("FINDING: case %d %s %s -- observed result differs from expected contract\n",
               g_n, method, scenario);
    g_ok += pass ? 1 : 0;
}
#define FINISH(m, s, p) do { int hs_p = (p); hs_emit((m), (s), hs_p); return hs_p; } while (0)

/* ================= strcpy (1-8) ========================================= */
static int c01(void) { /* exact fit: cap == strlen+1 */
    unsigned char d[4]; hs_fill(d, sizeof d);
    char *r = hs_strcpy((char *)d, 4, "abc");
    FINISH("strcpy", "exact_fit", r == (char *)d && memcmp(d, "abc\0", 4) == 0);
}
static int c02(void) { /* capacity-1: one byte short of NUL */
    unsigned char d[8]; hs_fill(d, sizeof d);
    char *r = hs_strcpy((char *)d, 3, "abcd");
    /* Checked layer copies a partial prefix before refusing; tail stays intact. */
    FINISH("strcpy", "capacity_minus_one", r == NULL && hs_canary(d, 3, 8));
}
static int c03(void) { /* capacity: generous destination */
    unsigned char d[16]; hs_fill(d, sizeof d);
    char *r = hs_strcpy((char *)d, 16, "abc");
    FINISH("strcpy", "capacity", r == (char *)d && strcmp((char *)d, "abc") == 0 &&
                                hs_canary(d, 4, 16));
}
static int c04(void) { /* capacity+1 */
    unsigned char d[5]; hs_fill(d, sizeof d);
    char *r = hs_strcpy((char *)d, 5, "abc");
    FINISH("strcpy", "capacity_plus_one", r == (char *)d && d[4] == HS_CANARY);
}
static int c05(void) { /* empty source */
    unsigned char d[4]; hs_fill(d, sizeof d);
    char *r = hs_strcpy((char *)d, 4, "");
    FINISH("strcpy", "empty", r == (char *)d && d[0] == '\0' && hs_canary(d, 1, 4));
}
static int c06(void) { /* embedded NUL: strlen stops early */
    unsigned char d[8]; hs_fill(d, sizeof d);
    char *r = hs_strcpy((char *)d, 8, "a\0bc");
    FINISH("strcpy", "embedded_nul", r == (char *)d && d[0] == 'a' && d[1] == '\0' &&
                                     hs_canary(d, 2, 8));
}
static int c07(void) { /* high-bit bytes */
    unsigned char d[8]; hs_fill(d, sizeof d);
    const char s[] = { (char)0xC3, (char)0xA9, (char)0x80, '\0' };
    char *r = hs_strcpy((char *)d, 8, s);
    FINISH("strcpy", "high_bit_bytes", r == (char *)d && memcmp(d, s, 4) == 0);
}
static int c08(void) { /* host differential on valid input */
    unsigned char a[24], b[24]; hs_fill(a, sizeof a); hs_fill(b, sizeof b);
    strcpy((char *)b, "host-differential");
    char *r = hs_strcpy((char *)a, 24, (char *)b);
    FINISH("strcpy", "host_differential", r == (char *)a && memcmp(a, b, 24) == 0);
}

/* ================= strncpy (9-16) ======================================= */
static int c09(void) { /* n < strlen: copy prefix only (checked layer adds NUL) */
    unsigned char d[12]; hs_fill(d, sizeof d);
    char *r = hs_strncpy((char *)d, 12, "abcdef", 3);
    FINISH("strncpy", "n_less_than_src", r == (char *)d && memcmp(d, "abc", 3) == 0 &&
                                         hs_canary(d, 4, 12));
}
static int c10(void) { /* exact fit: n == strlen, cap == n+1 */
    unsigned char d[7]; hs_fill(d, sizeof d);
    char *r = hs_strncpy((char *)d, 7, "abcdef", 6);
    /* Byte 6 is layer-specific: checked layer NUL-terminates, plain layer
     * leaves the canary; both agree on the six copied bytes. */
    FINISH("strncpy", "exact_fit", r == (char *)d && memcmp(d, "abcdef", 6) == 0 &&
                                    (d[6] == HS_CANARY || d[6] == '\0'));
}
static int c11(void) { /* n > strlen: source shorter than n */
    unsigned char d[12]; hs_fill(d, sizeof d);
    char *r = hs_strncpy((char *)d, 12, "ab", 8);
    /* Slack past the source NUL is layer-specific, so compare the string only. */
    FINISH("strncpy", "n_greater_than_src", r == (char *)d && strcmp((char *)d, "ab") == 0);
}
static int c12(void) { /* capacity-1: cap < n -> both layers refuse */
    unsigned char d[8]; hs_fill(d, sizeof d);
    char *r = hs_strncpy((char *)d, 3, "abcdefgh", 4);
    FINISH("strncpy", "capacity_minus_one", r == NULL && hs_canary(d, 3, 8));
}
static int c13(void) { /* empty (n == 0) */
    unsigned char d[4]; hs_fill(d, sizeof d); d[0] = '\0';
    char *r = hs_strncpy((char *)d, 4, "xyz", 0);
    FINISH("strncpy", "empty_n_zero", r == (char *)d && d[0] == '\0');
}
static int c14(void) { /* embedded NUL in source */
    unsigned char d[10]; hs_fill(d, sizeof d);
    char *r = hs_strncpy((char *)d, 10, "ab\0cd", 5);
    FINISH("strncpy", "embedded_nul", r == (char *)d && memcmp(d, "ab\0", 3) == 0 &&
                                      d[2] == '\0');
}
static int c15(void) { /* unterminated source with explicit bound */
    /* Static storage keeps the source away from the destination on the stack,
     * so safeclib's overlap detector cannot report adjacent-object false positives. */
    static const char src[4] = { 'a', 'b', 'c', 'd' }; /* deliberately no NUL */
    unsigned char d[8]; hs_fill(d, sizeof d); d[4] = '\0';
    char *r = hs_strncpy((char *)d, 8, src, 4);
    FINISH("strncpy", "unterminated_explicit_bound", r == (char *)d &&
                                                   memcmp(d, "abcd", 4) == 0 && d[4] == '\0');
}
static int c16(void) { /* host differential on valid input (compare strings,
                        * not the layer-specific slack bytes past the NUL) */
    unsigned char a[16], b[16]; hs_fill(a, sizeof a); hs_fill(b, sizeof b);
    strncpy((char *)b, "differential", 16);
    char *r = hs_strncpy((char *)a, 16, "differential", 16);
    FINISH("strncpy", "host_differential", r == (char *)a &&
                                          strcmp((char *)a, (char *)b) == 0);
}

/* ================= strcat (17-24) ======================================= */
static int c17(void) { /* exact fit */
    unsigned char d[6]; hs_fill(d, sizeof d); strcpy((char *)d, "ab");
    char *r = hs_strcat((char *)d, 6, "cde");
    FINISH("strcat", "exact_fit", r == (char *)d && memcmp(d, "abcde\0", 6) == 0);
}
static int c18(void) { /* capacity-1 -> refusal, canary intact */
    unsigned char d[8]; hs_fill(d, sizeof d); strcpy((char *)d, "ab");
    char *r = hs_strcat((char *)d, 5, "cde");
    /* Checked layer clears d[0] on refusal; both leave the tail intact. */
    FINISH("strcat", "capacity_minus_one", r == NULL && hs_canary(d, 5, 8));
}
static int c19(void) { /* capacity: generous */
    unsigned char d[24]; hs_fill(d, sizeof d); strcpy((char *)d, "ab");
    char *r = hs_strcat((char *)d, 24, "cdef");
    FINISH("strcat", "capacity", r == (char *)d && strcmp((char *)d, "abcdef") == 0 &&
                                hs_canary(d, 7, 24));
}
static int c20(void) { /* capacity+1 */
    unsigned char d[7]; hs_fill(d, sizeof d); strcpy((char *)d, "ab");
    char *r = hs_strcat((char *)d, 7, "cde");
    FINISH("strcat", "capacity_plus_one", r == (char *)d && d[6] == HS_CANARY);
}
static int c21(void) { /* empty source is a no-op */
    unsigned char d[8]; hs_fill(d, sizeof d); strcpy((char *)d, "ab");
    char *r = hs_strcat((char *)d, 8, "");
    FINISH("strcat", "empty", r == (char *)d && strcmp((char *)d, "ab") == 0);
}
static int c22(void) { /* embedded NUL in source */
    unsigned char d[8]; hs_fill(d, sizeof d); strcpy((char *)d, "ab");
    char *r = hs_strcat((char *)d, 8, "x\0y");
    FINISH("strcat", "embedded_nul", r == (char *)d && memcmp(d, "abx\0", 4) == 0);
}
static int c23(void) { /* repeated appends */
    unsigned char d[16]; int i; hs_fill(d, sizeof d); d[0] = '\0';
    for (i = 0; i < 6; ++i) if (!hs_strncat((char *)d, 16, "z", 1)) break;
    FINISH("strcat", "repeated_appends", strcmp((char *)d, "zzzzzz") == 0 &&
                                        hs_canary(d, 7, 16));
}
static int c24(void) { /* host differential */
    unsigned char a[24], b[24]; hs_fill(a, sizeof a); hs_fill(b, sizeof b);
    strcpy((char *)a, "left-"); strcpy((char *)b, "left-");
    hs_strcat((char *)a, 24, "right"); strcat((char *)b, "right");
    FINISH("strcat", "host_differential", memcmp(a, b, 24) == 0);
}

/* ================= strncat (25-32) ====================================== */
static int c25(void) { /* n < strlen */
    unsigned char d[12]; hs_fill(d, sizeof d); strcpy((char *)d, "ab");
    char *r = hs_strncat((char *)d, 12, "cdefgh", 3);
    FINISH("strncat", "n_less_than_src", r == (char *)d && strcmp((char *)d, "abcde") == 0);
}
static int c26(void) { /* exact fit: dlen+n+1 == cap */
    unsigned char d[6]; hs_fill(d, sizeof d); strcpy((char *)d, "ab");
    char *r = hs_strncat((char *)d, 6, "cdef", 3);
    FINISH("strncat", "exact_fit", r == (char *)d && memcmp(d, "abcde\0", 6) == 0);
}
static int c27(void) { /* capacity-1 -> refusal */
    unsigned char d[8]; hs_fill(d, sizeof d); strcpy((char *)d, "ab");
    char *r = hs_strncat((char *)d, 5, "cde", 3);
    FINISH("strncat", "capacity_minus_one", r == NULL && hs_canary(d, 5, 8));
}
static int c28(void) { /* n > strlen: append whole source */
    unsigned char d[12]; hs_fill(d, sizeof d); strcpy((char *)d, "ab");
    char *r = hs_strncat((char *)d, 12, "cde", 9);
    FINISH("strncat", "n_greater_than_src", r == (char *)d && strcmp((char *)d, "abcde") == 0);
}
static int c29(void) { /* n == 0 is a no-op */
    unsigned char d[8]; hs_fill(d, sizeof d); strcpy((char *)d, "ab");
    char *r = hs_strncat((char *)d, 8, "cdef", 0);
    FINISH("strncat", "empty_n_zero", r == (char *)d && strcmp((char *)d, "ab") == 0);
}
static int c30(void) { /* embedded NUL */
    unsigned char d[8]; hs_fill(d, sizeof d); strcpy((char *)d, "ab");
    char *r = hs_strncat((char *)d, 8, "x\0yz", 3);
    FINISH("strncat", "embedded_nul", r == (char *)d && memcmp(d, "abx\0", 4) == 0);
}
static int c31(void) { /* high-bit bytes */
    unsigned char d[8]; hs_fill(d, sizeof d); d[0] = '\0';
    const char s[] = { (char)0xE2, (char)0x82, (char)0xAC, '\0' };
    char *r = hs_strncat((char *)d, 8, s, 3);
    FINISH("strncat", "high_bit_bytes", r == (char *)d && memcmp(d, s, 4) == 0);
}
static int c32(void) { /* host differential */
    unsigned char a[16], b[16]; hs_fill(a, sizeof a); hs_fill(b, sizeof b);
    strcpy((char *)a, "p-"); strcpy((char *)b, "p-");
    hs_strncat((char *)a, 16, "qrstuv", 4); strncat((char *)b, "qrstuv", 4);
    FINISH("strncat", "host_differential", memcmp(a, b, 16) == 0);
}

/* ================= strlen (33-36) ======================================= */
static int c33(void) { return (hs_emit("strlen", "basic", cplus_strlen("hello") == 5), 1); }
static int c34(void) { return (hs_emit("strlen", "embedded_nul", cplus_strlen("a\0bcd") == 1), 1); }
static int c35(void) { return (hs_emit("strlen", "high_bit_bytes", cplus_strlen("\xC3\xA9\x80") == 3), 1); }
static int c36(void) {
    const char *s = "host-differential-string";
    int pass = cplus_strlen(s) == strlen(s);
    FINISH("strlen", "host_differential", pass);
}

/* ================= strnlen (37-40) ====================================== */
static int c37(void) { return (hs_emit("strnlen", "bound_less_than_len", cplus_strnlen("abcdef", 3) == 3), 1); }
static int c38(void) { return (hs_emit("strnlen", "bound_greater_than_len", cplus_strnlen("abc", 10) == 3), 1); }
static int c39(void) {
    const char s[4] = { 'w', 'x', 'y', 'z' }; /* no NUL */
    FINISH("strnlen", "unterminated_explicit_bound", cplus_strnlen(s, 4) == 4);
}
static int c40(void) {
    size_t huge = ((size_t)-1) >> 1; /* RSIZE_MAX_STR: large but permitted */
    int pass = cplus_strnlen("abc", huge) == 3 && cplus_strnlen("ab\0cd", huge) == 2;
    FINISH("strnlen", "huge_length", pass);
}

/* ================= strcmp (41-44) ======================================= */
static int c41(void) { return (hs_emit("strcmp", "equal", cplus_strcmp("abc", "abc") == 0), 1); }
static int c42(void) { return (hs_emit("strcmp", "less", cplus_strcmp("abc", "abd") < 0), 1); }
static int c43(void) { return (hs_emit("strcmp", "greater", cplus_strcmp("abd", "abc") > 0), 1); }
static int c44(void) {
    int pass = cplus_strcmp("ab\0z", "ab") == 0 && strcmp("ab\0z", "ab") == 0;
    FINISH("strcmp", "embedded_nul", pass);
}

/* ================= strncmp (45-48) ====================================== */
static int c45(void) { return (hs_emit("strncmp", "equal_within_n", cplus_strncmp("abcd", "abce", 3) == 0), 1); }
static int c46(void) { return (hs_emit("strncmp", "differ_at_last", cplus_strncmp("abcd", "abce", 4) < 0), 1); }
static int c47(void) { return (hs_emit("strncmp", "differ_beyond_n", cplus_strncmp("abcd", "abce", 2) == 0), 1); }
static int c48(void) { return (hs_emit("strncmp", "n_zero", cplus_strncmp("abc", "xyz", 0) == 0), 1); }

/* ================= strchr (49-52) ======================================= */
static int c49(void) {
    const char s[] = "hello";
    FINISH("strchr", "found", cplus_strchr(s, 'l') == s + 2);
}
static int c50(void) { return (hs_emit("strchr", "not_found", cplus_strchr("hello", 'z') == NULL), 1); }
static int c51(void) {
    const char s[] = "abc";
    FINISH("strchr", "search_nul", cplus_strchr(s, '\0') == s + 3);
}
static int c52(void) {
    const char s[] = { (char)0xC3, (char)0xA9, '\0' }; /* high-bit byte search */
    FINISH("strchr", "high_bit_bytes", cplus_strchr(s, (unsigned char)0xA9) == s + 1);
}

/* ================= strrchr (53-55) ====================================== */
static int c53(void) {
    const char s[] = "a/b/c";
    FINISH("strrchr", "found_last", cplus_strrchr(s, '/') == s + 3);
}
static int c54(void) { return (hs_emit("strrchr", "not_found", cplus_strrchr("abc", '!') == NULL), 1); }
static int c55(void) {
    const char s[] = "abc";
    FINISH("strrchr", "search_nul", cplus_strrchr(s, '\0') == s + 3);
}

/* ================= strstr (56-58) ======================================= */
static int c56(void) {
    const char s[] = "abcde";
    FINISH("strstr", "found", cplus_strstr(s, "cd") == s + 2);
}
static int c57(void) { return (hs_emit("strstr", "not_found", cplus_strstr("abcde", "xy") == NULL), 1); }
static int c58(void) {
    const char s[] = "abc";
    FINISH("strstr", "empty_needle", cplus_strstr(s, "") == s);
}

/* ================= strdup (59-61) ======================================= */
static int c59(void) {
    char *p = cplus_strdup("duplicate-me");
    int pass = p && strcmp(p, "duplicate-me") == 0;
    if (p) cplus_free(p);
    FINISH("strdup", "valid", pass);
}
static int c60(void) {
    char *p = cplus_strdup("");
    int pass = p && p[0] == '\0';
    if (p) cplus_free(p);
    FINISH("strdup", "empty", pass);
}
static int c61(void) {
    char *p = cplus_strdup("embedded\0tail");
    int pass = p && memcmp(p, "embedded\0", 9) == 0;
    if (p) cplus_free(p);
    FINISH("strdup", "embedded_nul", pass);
}

/* ================= strndup (62-64) ====================================== */
static int c62(void) {
    char *p = cplus_strndup("abcdef", 3);
    int pass = p && strcmp(p, "abc") == 0;
    if (p) cplus_free(p);
    FINISH("strndup", "n_less_than_len", pass);
}
static int c63(void) {
    char *p = cplus_strndup("abc", 10);
    int pass = p && strcmp(p, "abc") == 0;
    if (p) cplus_free(p);
    FINISH("strndup", "n_greater_than_len", pass);
}
static int c64(void) {
    char *p = cplus_strndup("ab\0cd", 5);
    int pass = p && memcmp(p, "ab\0", 3) == 0;
    if (p) cplus_free(p);
    FINISH("strndup", "embedded_nul", pass);
}

/* ================= strtok (65-67) ======================================= */
static int c65(void) {
    char s[] = "one two three"; char *t;
    t = cplus_strtok(s, " ");
    FINISH("strtok", "basic", t && strcmp(t, "one") == 0);
}
static int c66(void) {
    char s[] = "a,,b"; char *t; int n = 0;
    for (t = cplus_strtok(s, ","); t; t = cplus_strtok(NULL, ",")) ++n;
    FINISH("strtok", "consecutive_delims", n == 2);
}
static int c67(void) {
    char s[] = "abc"; char *t = cplus_strtok(s, ",");
    FINISH("strtok", "no_delimiter", t && strcmp(t, "abc") == 0);
}

/* ================= strtok_r (68-70) ===================================== */
static int c68(void) {
    char s[] = "x-y-z"; char *save = NULL; char *t; int n = 0;
    for (t = cplus_strtok_r(s, "-", &save); t; t = cplus_strtok_r(NULL, "-", &save)) ++n;
    FINISH("strtok_r", "multiple_tokens", n == 3);
}
static int c69(void) {
    char s[] = "abc"; char *save = NULL; char *t = cplus_strtok_r(s, ",", &save);
    FINISH("strtok_r", "no_delimiter", t && strcmp(t, "abc") == 0);
}
static int c70(void) {
    char s[] = " a b "; char *save = NULL; char *t = cplus_strtok_r(s, " ", &save);
    FINISH("strtok_r", "leading_delims_skipped", t && strcmp(t, "a") == 0);
}

/* ================= strsep (71-73) ======================================= */
static int c71(void) {
    char s[] = "a:b:c"; char *p = s; char *t = cplus_strsep(&p, ":");
    FINISH("strsep", "basic", t && strcmp(t, "a") == 0 && p && strcmp(p, "b:c") == 0);
}
static int c72(void) {
    char s[] = "a::b"; char *p = s; char *t = cplus_strsep(&p, ":"); int empty = 0;
    (void)t; t = cplus_strsep(&p, ":"); empty = (t && t[0] == '\0');
    FINISH("strsep", "consecutive_delims", empty);
}
static int c73(void) {
    char s[] = "abc"; char *p = s; char *t = cplus_strsep(&p, ":");
    FINISH("strsep", "no_delimiter", t && strcmp(t, "abc") == 0 && p == NULL);
}

/* ================= strspn (74-76) ======================================= */
static int c74(void) { return (hs_emit("strspn", "basic", cplus_strspn("abc123", "abc") == 3), 1); }
static int c75(void) { return (hs_emit("strspn", "none_match", cplus_strspn("xyz", "abc") == 0), 1); }
static int c76(void) { return (hs_emit("strspn", "all_match", cplus_strspn("aaa", "abc") == 3), 1); }

/* ================= strcspn (77-79) ====================================== */
static int c77(void) { return (hs_emit("strcspn", "basic", cplus_strcspn("abc123", "123") == 3), 1); }
static int c78(void) { return (hs_emit("strcspn", "none_match", cplus_strcspn("abc", "xyz") == 3), 1); }
static int c79(void) { return (hs_emit("strcspn", "first_byte_match", cplus_strcspn("abc", "a") == 0), 1); }

/* ================= strpbrk (80-82) ====================================== */
static int c80(void) {
    const char s[] = "abcde";
    FINISH("strpbrk", "found", cplus_strpbrk(s, "cd") == s + 2);
}
static int c81(void) { return (hs_emit("strpbrk", "not_found", cplus_strpbrk("abc", "xyz") == NULL), 1); }
static int c82(void) {
    const char s[] = { (char)0xC3, (char)0xA9, '\0' };
    const char set[] = { (char)0xA9, '\0' };
    FINISH("strpbrk", "high_bit_bytes", cplus_strpbrk(s, set) == s + 1);
}

/* ================= stpcpy (83-85) ======================================= */
static int c83(void) {
    unsigned char d[8]; hs_fill(d, sizeof d);
    char *end = cplus_stpcpy((char *)d, "abc");
    FINISH("stpcpy", "returns_terminator", end == (char *)d + 3 && memcmp(d, "abc\0", 4) == 0);
}
static int c84(void) {
    unsigned char d[8]; hs_fill(d, sizeof d);
    char *end = cplus_stpcpy((char *)d, "");
    FINISH("stpcpy", "empty", end == (char *)d && d[0] == '\0');
}
static int c85(void) {
    unsigned char d[8]; hs_fill(d, sizeof d);
    char *end = cplus_stpcpy((char *)d, "a\0b");
    FINISH("stpcpy", "embedded_nul", end == (char *)d + 1 && d[0] == 'a' && d[1] == '\0');
}

/* ================= stpncpy (86-88) ====================================== */
static int c86(void) {
    unsigned char d[8]; hs_fill(d, sizeof d);
    char *end = cplus_stpncpy((char *)d, "abc", 3);
    /* With n == strlen no terminator is written; the byte past n is canary. */
    FINISH("stpncpy", "n_equals_len", end == (char *)d + 3 && memcmp(d, "abc", 3) == 0 &&
                                      d[3] == HS_CANARY);
}
static int c87(void) {
    unsigned char d[8]; hs_fill(d, sizeof d);
    char *end = cplus_stpncpy((char *)d, "ab", 5);
    FINISH("stpncpy", "n_greater_than_len", end == (char *)d + 2 && d[2] == '\0' && d[4] == '\0');
}
static int c88(void) {
    unsigned char d[8]; hs_fill(d, sizeof d);
    char *end = cplus_stpncpy((char *)d, "abcdef", 3);
    FINISH("stpncpy", "n_less_than_len", end == (char *)d + 3 && memcmp(d, "abc", 3) == 0);
}

/* ================= checked layer (89-96) ================================= */
static int c89(void) { /* strcpy_s NULL dest: guarded refusal */
    int rc = hs_strcpy_s(NULL, 8, "abc");
    FINISH("strcpy_s", "null_guarded", rc != 0);
}
static int c90(void) { /* strncpy_s canary integrity on refusal */
    unsigned char d[8]; hs_fill(d, sizeof d);
    int rc = hs_strncpy_s((char *)d, 4, "abcdefgh", 6);
    FINISH("strncpy_s", "canary_integrity", rc != 0 && hs_canary(d, 4, 8));
}
static int c91(void) { /* strcat_s embedded NUL source */
    unsigned char d[8]; hs_fill(d, sizeof d); strcpy((char *)d, "ab");
    int rc = hs_strcat_s((char *)d, 8, "x\0y");
    FINISH("strcat_s", "embedded_nul", rc == 0 && memcmp(d, "abx\0", 4) == 0);
}
static int c92(void) { /* checked API: defined self-copy, refused overlap */
    unsigned char d[16]; hs_fill(d, sizeof d); strcpy((char *)d, "abc");
    int self_ok = hs_strcpy_s((char *)d, 16, (char *)d) == 0;
    unsigned char e[16]; hs_fill(e, sizeof e); strcpy((char *)e, "abcd");
    int ovl_refused = hs_strcpy_s((char *)e, 16, (char *)e + 1) != 0;
    FINISH("strcpy_s", "self_copy_and_overlap", self_ok && ovl_refused);
}
static int c93(void) { /* strnlen_s NULL guard */
    FINISH("strnlen_s", "null_guarded", hs_strnlen_s(NULL, 10) == 0);
}
static int c94(void) { /* strnlen_s huge bound (safeclib RSIZE_MAX_STR = 4096) */
    size_t huge = 4096u;
    FINISH("strnlen_s", "huge_length", hs_strnlen_s("abc", huge) == 3);
}
static int c95(void) { /* strcmp_s indicator on mismatch */
    int ind = 99; int rc = hs_strcmp_s("abc", 3, "abd", &ind);
    FINISH("strcmp_s", "mismatch_indicator", rc == 0 && ind < 0);
}
static int c96(void) { /* strtok_s tokenization */
    char s[] = "p,q,r"; size_t dmax = sizeof s; char *save = NULL;
    char *t = hs_strtok_s(s, &dmax, ",", &save);
    FINISH("strtok_s", "tokenization", t && strcmp(t, "p") == 0);
}

/* ================= cross-cutting invariants (97-100) ===================== */
static int c97(void) { /* logging OFF/ON parity */
    unsigned char a[8], b[8]; char *ra, *rb;
    hs_log(0); hs_fill(a, sizeof a); a[0] = '\0'; ra = hs_strcat((char *)a, 8, "abcdefgh");
    hs_log(1); hs_fill(b, sizeof b); b[0] = '\0'; rb = hs_strcat((char *)b, 8, "abcdefgh");
    hs_log(0);
    /* On refusal the checked layer leaves a partial copy, so compare the two
     * buffers rather than asserting canary preservation. */
    FINISH("logging", "off_on_parity", (ra == NULL) == (rb == NULL) &&
                                      memcmp(a, b, 8) == 0);
}
static int c98(void) { /* post-realloc alias with tracked capacity */
    char *p = hs_alloc(8), *q; char *r; int pass;
    if (!p) FINISH("strcpy", "post_realloc_alias", 0);
    memcpy(p, "abc", 4);
    q = hs_realloc(p, 16);
    if (!q) { hs_free(p); FINISH("strcpy", "post_realloc_alias", 0); }
    r = hs_strcpy(q, hs_cap(q), "abcdefghij");
    pass = r == q && strcmp(q, "abcdefghij") == 0;
    hs_free(q);
    FINISH("strcpy", "post_realloc_alias", pass);
}
static int c99(void) { /* canary integrity across repeated appends */
    unsigned char d[24]; int i; hs_fill(d, sizeof d); d[0] = '\0';
    for (i = 0; i < 5; ++i) if (!hs_strncat((char *)d, 20, "ab", 2)) break;
    FINISH("strncat", "canary_integrity", strcmp((char *)d, "ababababab") == 0 &&
                                         hs_canary(d, 11, 24));
}
static int c100(void) { /* NULL guarded across all bounded mutators */
    unsigned char d[8]; hs_fill(d, sizeof d); strcpy((char *)d, "d");
    int pass =
        hs_strcpy(NULL, 8, "x") == NULL &&
        hs_strcpy((char *)d, 8, NULL) == NULL &&
        hs_strncpy(NULL, 8, "x", 1) == NULL &&
        hs_strncpy((char *)d, 8, NULL, 1) == NULL &&
        hs_strcat(NULL, 8, "x") == NULL &&
        hs_strcat((char *)d, 8, NULL) == NULL &&
        hs_strncat(NULL, 8, "x", 1) == NULL &&
        hs_strncat((char *)d, 8, NULL, 1) == NULL &&
        strcmp((char *)d, "d") == 0;
    FINISH("bounded", "null_guarded_all", pass);
}

typedef int (*hs_case_fn)(void);
static hs_case_fn hs_cases[100] = {
    c01,c02,c03,c04,c05,c06,c07,c08,c09,c10,
    c11,c12,c13,c14,c15,c16,c17,c18,c19,c20,
    c21,c22,c23,c24,c25,c26,c27,c28,c29,c30,
    c31,c32,c33,c34,c35,c36,c37,c38,c39,c40,
    c41,c42,c43,c44,c45,c46,c47,c48,c49,c50,
    c51,c52,c53,c54,c55,c56,c57,c58,c59,c60,
    c61,c62,c63,c64,c65,c66,c67,c68,c69,c70,
    c71,c72,c73,c74,c75,c76,c77,c78,c79,c80,
    c81,c82,c83,c84,c85,c86,c87,c88,c89,c90,
    c91,c92,c93,c94,c95,c96,c97,c98,c99,c100
};

int main(void) {
    int i;
#ifdef HS_SAFECRT
    /* Inspect NULL/ERANGE/refusal return values instead of trapping. */
    cplus_runtime_set_violation_trap(0);
#endif
    for (i = 0; i < 100; ++i) (void)hs_cases[i]();
    printf("hundred_string: %d/100 scenarios ok\n", g_ok);
    return 0;
}
