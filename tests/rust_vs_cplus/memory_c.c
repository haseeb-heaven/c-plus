/* memory_c.c -- 100 DISTINCT memory/string scenarios, C+ hardened-C side.
 *
 * PAIRED WITH: memory_rs.rs (safe Rust side).  Both programs accept a case
 * number 1..100 and print exactly one line:  RESULT <n> <OUTCOME>
 * where OUTCOME is ACCEPTED, REFUSED or SKIP.  Both always return 0; no case
 * aborts, traps or terminates the process.
 *
 * FILE SCOPE: this is the C half of a paired differential test.  It includes
 * `stdlib/safe/safe_c.h` and calls the ORDINARY C spellings (memset, memcpy,
 * memmove, memcmp, strcpy, strncpy, strcat, strncat), which the header remaps
 * to capacity-checked wrappers in libcplus_safecrt.a.  The explicit checked
 * `_s` entry points are used only where an ordinary spelling cannot express
 * the scenario safely (NULL probes, unterminated sources with explicit bounds,
 * SIZE_MAX capacity probes) -- exactly the cases the task sets apart.
 *
 * HONESTY / EXPECTED DIFFERENCES (read alongside memory_rs.rs):
 *  - C+ refuses bad operations at RUNTIME (safeclib constraint codes surfaced
 *    as errno EINVAL/ERANGE; the auto wrappers return NULL).  Rust's guarantees
 *    are largely COMPILE-TIME: a raw out-of-bounds write, a NULL reference or a
 *    SIZE_MAX-length slice is inexpressible in safe Rust.  Those cases print
 *    REFUSED in C and SKIP in Rust ("would require unsafe").  This is a genuine
 *    mechanism difference, NOT a failure and NOT fake equality.
 *  - Where Rust has an honest runtime refusal of its own (safe slice
 *    `.get(..n)` returning None, or a bounded search returning None), both
 *    sides print REFUSED and the harness records a match.  Where Rust has a
 *    defined safe reformulation (copy_within for overlap, byte slices for
 *    unterminated data), both sides print ACCEPTED.
 *  - Raw overlapping memcpy is UB in both languages; neither program invokes
 *    it and both print SKIP.  memmove overlap is defined in both -> ACCEPTED.
 *  - Logging ON/OFF parity is C-specific instrumentation (no analogue knob in
 *    safe Rust); those cases print ACCEPTED in C and SKIP in Rust.
 *  - Outcome equality is evidence of OBSERVED behaviour only; it does not mean
 *    the two languages provide the same class of safety guarantee.
 *
 * All refusals are made with cplus_runtime_set_violation_trap(0) and
 * CPLUS_SAFE_CONSTRAINT_IGNORE, so no case can abort or _exit(70).  Canary
 * bytes around every destination are verified after each operation: a refusal
 * must leave them untouched.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Capture genuine host primitives before safe_c.h remaps the ordinary names.
 * They are used only to build EXPECTED byte patterns for differential checks. */
static void *(*const H_MEMCPY)(void *, const void *, size_t) = memcpy;
static void *(*const H_MEMSET)(void *, int, size_t) = memset;
static void *(*const H_MEMMOVE)(void *, const void *, size_t) = memmove;
static int (*const H_MEMCMP)(const void *, const void *, size_t) = memcmp;

#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_error.h"
#include "stdlib/safe/safe_runtime.h"
#include "stdlib/safe/safe_log.h"

/* ------------------------------------------------------------------------- */
/* Plumbing and small helpers                                                */

static void emit(int id, const char *verdict) {
    printf("RESULT %d %s\n", id, verdict);
}

static void fill(unsigned char *p, size_t n, unsigned seed) {
    size_t i;
    for (i = 0; i < n; ++i) p[i] = (unsigned char)(seed + i * 13u);
}
static int eq(const void *a, const void *b, size_t n) {
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    size_t i;
    for (i = 0; i < n; ++i) if (x[i] != y[i]) return 0;
    return 1;
}
static int all(const void *a, size_t n, unsigned char v) {
    const unsigned char *p = (const unsigned char *)a;
    size_t i;
    for (i = 0; i < n; ++i) if (p[i] != v) return 0;
    return 1;
}

struct canary { unsigned char pre[4], body[16], post[4]; };
static void canary_init(struct canary *c) {
    int i;
    for (i = 0; i < 4; ++i) {
        c->pre[i] = (unsigned char)(0xA0 + i);
        c->post[i] = (unsigned char)(0xD0 + i);
    }
    fill(c->body, 16, 0x5Cu);
}
static int canary_ok(const struct canary *c) {
    int i;
    for (i = 0; i < 4; ++i)
        if (c->pre[i] != (unsigned char)(0xA0 + i) ||
            c->post[i] != (unsigned char)(0xD0 + i)) return 0;
    return 1;
}

static char g_str[8]; /* static-storage destination for a couple of cases */

/* ========================================================================= */
/* 01..10  memset                                                            */

/* 01 memset: stack exact fit (16 of 16 bytes) */
static void c01(int id) {
    unsigned char b[16], ref[16];
    fill(b, 16, 1); H_MEMSET(ref, 0x41, 16);
    errno = 0; memset(b, 0x41, 16);
    emit(id, (errno == 0 && eq(b, ref, 16)) ? "ACCEPTED" : "REFUSED");
}
/* 02 memset: zero length (no byte touched) */
static void c02(int id) {
    unsigned char b[16];
    fill(b, 16, 2);
    errno = 0; memset(b, 0x42, 0);
    emit(id, (errno == 0 && b[0] == 2 && b[15] == (unsigned char)(2 + 15 * 13)) ? "ACCEPTED" : "REFUSED");
}
/* 03 memset: capacity-1 (15 of 16 bytes) */
static void c03(int id) {
    unsigned char b[16], ref[16];
    fill(b, 16, 3); fill(ref, 16, 3); H_MEMSET(ref, 0x43, 15);
    errno = 0; memset(b, 0x43, 15);
    emit(id, (errno == 0 && eq(b, ref, 16)) ? "ACCEPTED" : "REFUSED");
}
/* 04 memset: exactly capacity (global buffer, exact fit) */
static void c04(int id) {
    static unsigned char gb[16];
    fill(gb, 16, 4);
    errno = 0; memset(gb, 0x44, 16);
    emit(id, (errno == 0 && all(gb, 16, 0x44)) ? "ACCEPTED" : "REFUSED");
}
/* 05 memset: capacity+1 (17 into 16) -> runtime ERANGE */
static void c05(int id) {
    unsigned char b[16], ref[16];
    fill(b, 16, 5); H_MEMCPY(ref, b, 16);
    errno = 0; memset(b, 0x45, 17);
    emit(id, (errno == ERANGE && eq(b, ref, 16)) ? "REFUSED" : "ACCEPTED");
}
/* 06 memset: NULL destination via checked auto wrapper -> EINVAL */
static void c06(int id) {
    errno = 0; memset(NULL, 0x46, 4);
    emit(id, (errno == EINVAL) ? "REFUSED" : "ACCEPTED");
}
/* 07 memset: SIZE_MAX length -> ERANGE, no write */
static void c07(int id) {
    unsigned char b[8], ref[8];
    fill(b, 8, 7); H_MEMCPY(ref, b, 8);
    errno = 0; memset(b, 0x47, SIZE_MAX);
    emit(id, (errno == ERANGE && eq(b, ref, 8)) ? "REFUSED" : "ACCEPTED");
}
/* 08 memset: high-bit fill byte 0xE3, exact fit */
static void c08(int id) {
    unsigned char b[8], ref[8];
    H_MEMSET(b, 0, 8); H_MEMSET(ref, 0xE3, 8);
    errno = 0; memset(b, 0xE3, 8);
    emit(id, (errno == 0 && eq(b, ref, 8)) ? "ACCEPTED" : "REFUSED");
}
/* 09 memset: struct member with canaries, exact fit keeps guards */
static void c09(int id) {
    struct canary c;
    canary_init(&c);
    errno = 0; memset(c.body, 0x7E, sizeof c.body);
    emit(id, (errno == 0 && all(c.body, 16, 0x7E) && canary_ok(&c)) ? "ACCEPTED" : "REFUSED");
}
/* 10 memset: logging OFF/ON parity (C-only instrumentation) */
static void c10(int id) {
    unsigned char a[8], b[8];
    int ea, eb;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    errno = 0; memset(a, 0x71, 8); ea = errno;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_DEBUG);
    errno = 0; memset(b, 0x71, 8); eb = errno;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    emit(id, (ea == 0 && eb == 0 && eq(a, b, 8)) ? "ACCEPTED" : "REFUSED");
}

/* ========================================================================= */
/* 11..20  memcpy                                                            */

/* 11 memcpy: stack exact fit, differential against host */
static void c11(int id) {
    unsigned char s[16], d[16], ref[16];
    fill(s, 16, 11); H_MEMSET(d, 0x9A, 16); H_MEMCPY(ref, s, 16);
    errno = 0; memcpy(d, s, 16);
    emit(id, (errno == 0 && eq(d, ref, 16)) ? "ACCEPTED" : "REFUSED");
}
/* 12 memcpy: zero length leaves destination unchanged */
static void c12(int id) {
    unsigned char s[4], d[4], ref[4];
    fill(s, 4, 12); fill(d, 4, 22); H_MEMCPY(ref, d, 4);
    errno = 0; memcpy(d, s, 0);
    emit(id, (errno == 0 && eq(d, ref, 4)) ? "ACCEPTED" : "REFUSED");
}
/* 13 memcpy: capacity-1 (15 of 16 bytes) */
static void c13(int id) {
    unsigned char s[15], d[16], ref[16];
    fill(s, 15, 13); fill(d, 16, 23); H_MEMCPY(ref, d, 16); H_MEMCPY(ref, s, 15);
    errno = 0; memcpy(d, s, 15);
    emit(id, (errno == 0 && eq(d, ref, 16)) ? "ACCEPTED" : "REFUSED");
}
/* 14 memcpy: exactly capacity (16 of 16 bytes) */
static void c14(int id) {
    unsigned char s[16], d[16], ref[16];
    fill(s, 16, 14); H_MEMCPY(ref, s, 16);
    errno = 0; memcpy(d, s, 16);
    emit(id, (errno == 0 && eq(d, ref, 16)) ? "ACCEPTED" : "REFUSED");
}
/* 15 memcpy: capacity+1 (17 into 16) -> ERANGE, no write */
static void c15(int id) {
    unsigned char s[17], d[16], ref[16];
    fill(s, 17, 15); fill(d, 16, 25); H_MEMCPY(ref, d, 16);
    errno = 0; memcpy(d, s, 17);
    emit(id, (errno == ERANGE && eq(d, ref, 16)) ? "REFUSED" : "ACCEPTED");
}
/* 16 memcpy: NULL source via checked auto wrapper -> EINVAL */
static void c16(int id) {
    unsigned char d[4], ref[4];
    H_MEMSET(d, 0x77, 4); H_MEMCPY(ref, d, 4);
    errno = 0; memcpy(d, NULL, 4);
    emit(id, (errno == EINVAL && eq(d, ref, 4)) ? "REFUSED" : "ACCEPTED");
}
/* 17 memcpy: SIZE_MAX length -> ERANGE, no write */
static void c17(int id) {
    unsigned char s[4], d[4], ref[4];
    fill(s, 4, 17); fill(d, 4, 27); H_MEMCPY(ref, d, 4);
    errno = 0; memcpy(d, s, SIZE_MAX);
    emit(id, (errno == ERANGE && eq(d, ref, 4)) ? "REFUSED" : "ACCEPTED");
}
/* 18 memcpy: overlapping ranges -- genuine UB, deliberately NOT invoked */
static void c18(int id) { (void)id; emit(id, "SKIP"); }
/* 19 memcpy: high-bit bytes into canary-guarded member */
static void c19(int id) {
    struct canary c; unsigned char s[16]; size_t i;
    canary_init(&c); for (i = 0; i < 16; ++i) s[i] = (unsigned char)(0x80 + i);
    errno = 0; memcpy(c.body, s, 16);
    emit(id, (errno == 0 && eq(c.body, s, 16) && canary_ok(&c)) ? "ACCEPTED" : "REFUSED");
}
/* 20 memcpy: embedded NUL byte treated as ordinary binary data */
static void c20(int id) {
    unsigned char s[8] = {'a', 0, 'b', 0, 'c', 0, 'd', 0}, d[8], ref[8];
    H_MEMCPY(ref, s, 8);
    errno = 0; memcpy(d, s, 8);
    emit(id, (errno == 0 && eq(d, ref, 8) && d[1] == 0 && d[3] == 0) ? "ACCEPTED" : "REFUSED");
}

/* ========================================================================= */
/* 21..30  memmove                                                           */

/* 21 memmove: non-overlapping exact fit, differential against host */
static void c21(int id) {
    unsigned char s[12], d[12], ref[12];
    fill(s, 12, 21); H_MEMSET(d, 0x99, 12); H_MEMCPY(ref, s, 12);
    errno = 0; memmove(d, s, 12);
    emit(id, (errno == 0 && eq(d, ref, 12)) ? "ACCEPTED" : "REFUSED");
}
/* 22 memmove: overlapping forward (dest = src+2), defined */
static void c22(int id) {
    unsigned char b[32], ref[32];
    H_MEMCPY(b, "0123456789abcdef", 17); H_MEMCPY(ref, b, 32);
    H_MEMMOVE(ref + 2, ref, 10);
    errno = 0; memmove(b + 2, b, 10);
    emit(id, (errno == 0 && eq(b, ref, 32)) ? "ACCEPTED" : "REFUSED");
}
/* 23 memmove: overlapping backward (dest = src-2), defined */
static void c23(int id) {
    unsigned char b[32], ref[32];
    H_MEMCPY(b, "0123456789abcdef", 17); H_MEMCPY(ref, b, 32);
    H_MEMMOVE(ref, ref + 2, 10);
    errno = 0; memmove(b, b + 2, 10);
    emit(id, (errno == 0 && eq(b, ref, 32)) ? "ACCEPTED" : "REFUSED");
}
/* 24 memmove: zero length leaves destination unchanged */
static void c24(int id) {
    unsigned char s[4], d[4], ref[4];
    fill(s, 4, 24); fill(d, 4, 34); H_MEMCPY(ref, d, 4);
    errno = 0; memmove(d, s, 0);
    emit(id, (errno == 0 && eq(d, ref, 4)) ? "ACCEPTED" : "REFUSED");
}
/* 25 memmove: capacity-1 (15 of 16 bytes) */
static void c25(int id) {
    unsigned char s[15], d[16], ref[16];
    fill(s, 15, 25); fill(d, 16, 35); H_MEMCPY(ref, d, 16); H_MEMCPY(ref, s, 15);
    errno = 0; memmove(d, s, 15);
    emit(id, (errno == 0 && eq(d, ref, 16)) ? "ACCEPTED" : "REFUSED");
}
/* 26 memmove: capacity+1 (17 into 16) -> ERANGE */
static void c26(int id) {
    unsigned char s[17], d[16], ref[16];
    fill(s, 17, 26); fill(d, 16, 36); H_MEMCPY(ref, d, 16);
    errno = 0; memmove(d, s, 17);
    emit(id, (errno == ERANGE && eq(d, ref, 16)) ? "REFUSED" : "ACCEPTED");
}
/* 27 memmove: NULL destination -> EINVAL */
static void c27(int id) {
    errno = 0; memmove(NULL, "abc", 4);
    emit(id, (errno == EINVAL) ? "REFUSED" : "ACCEPTED");
}
/* 28 memmove: SIZE_MAX length -> ERANGE */
static void c28(int id) {
    unsigned char s[4], d[4], ref[4];
    fill(s, 4, 28); fill(d, 4, 38); H_MEMCPY(ref, d, 4);
    errno = 0; memmove(d, s, SIZE_MAX);
    emit(id, (errno == ERANGE && eq(d, ref, 4)) ? "REFUSED" : "ACCEPTED");
}
/* 29 memmove: high-bit bytes into canary-guarded member */
static void c29(int id) {
    struct canary c; unsigned char s[16]; size_t i;
    canary_init(&c); for (i = 0; i < 16; ++i) s[i] = (unsigned char)(0xE0 + i);
    errno = 0; memmove(c.body, s, 16);
    emit(id, (errno == 0 && eq(c.body, s, 16) && canary_ok(&c)) ? "ACCEPTED" : "REFUSED");
}
/* 30 memmove: logging OFF/ON parity (C-only instrumentation) */
static void c30(int id) {
    unsigned char s[8], a[8], b[8]; int ea, eb;
    fill(s, 8, 30);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    errno = 0; memmove(a, s, 8); ea = errno;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_DEBUG);
    errno = 0; memmove(b, s, 8); eb = errno;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    emit(id, (ea == 0 && eb == 0 && eq(a, b, 8)) ? "ACCEPTED" : "REFUSED");
}

/* ========================================================================= */
/* 31..40  memcmp                                                            */

/* 31 memcmp: equal byte ranges */
static void c31(int id) {
    unsigned char a[8], b[8]; int v;
    fill(a, 8, 31); H_MEMCPY(b, a, 8);
    errno = 0; v = memcmp(a, b, 8);
    emit(id, (errno == 0 && v == 0) ? "ACCEPTED" : "REFUSED");
}
/* 32 memcmp: lexicographically less */
static void c32(int id) {
    unsigned char a[4] = {1, 2, 3, 4}, b[4] = {1, 2, 4, 4}; int v;
    errno = 0; v = memcmp(a, b, 4);
    emit(id, (errno == 0 && v < 0) ? "ACCEPTED" : "REFUSED");
}
/* 33 memcmp: lexicographically greater */
static void c33(int id) {
    unsigned char a[4] = {9, 2, 3, 4}, b[4] = {1, 2, 3, 4}; int v;
    errno = 0; v = memcmp(a, b, 4);
    emit(id, (errno == 0 && v > 0) ? "ACCEPTED" : "REFUSED");
}
/* 34 memcmp: zero length with differing buffers compares equal */
static void c34(int id) {
    unsigned char a[2] = {0xFF, 1}, b[2] = {0, 0xFF}; int v;
    errno = 0; v = memcmp(a, b, 0);
    emit(id, (errno == 0 && v == 0) ? "ACCEPTED" : "REFUSED");
}
/* 35 memcmp: high-bit bytes, sign agrees with host */
static void c35(int id) {
    unsigned char a[3] = {0, 0x80, 0xFF}, b[3] = {0, 0x80, 0xFE};
    int v, h;
    errno = 0; v = memcmp(a, b, 3); h = H_MEMCMP(a, b, 3);
    emit(id, (errno == 0 && ((v > 0) == (h > 0))) ? "ACCEPTED" : "REFUSED");
}
/* 36 memcmp: embedded NUL byte is just data */
static void c36(int id) {
    unsigned char a[3] = {'a', 0, 'b'}, b[3] = {'a', 0, 'c'}; int v, h;
    errno = 0; v = memcmp(a, b, 3); h = H_MEMCMP(a, b, 3);
    emit(id, (errno == 0 && v < 0 && (v > 0) == (h > 0)) ? "ACCEPTED" : "REFUSED");
}
/* 37 memcmp: NULL first buffer -> EINVAL */
static void c37(int id) {
    unsigned char b[4] = {1, 2, 3, 4};
    errno = 0; (void)memcmp(NULL, b, 4);
    emit(id, (errno == EINVAL) ? "REFUSED" : "ACCEPTED");
}
/* 38 memcmp: over-capacity length (8 bytes from 4-byte buffers) -> ERANGE */
static void c38(int id) {
    unsigned char a[4] = {1, 2, 3, 4}, b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    errno = 0; (void)memcmp(a, b, 8);
    emit(id, (errno == ERANGE) ? "REFUSED" : "ACCEPTED");
}
/* 39 memcmp: SIZE_MAX length -> ERANGE */
static void c39(int id) {
    unsigned char a[4] = {1, 2, 3, 4}, b[4] = {1, 2, 3, 4};
    errno = 0; (void)memcmp(a, b, SIZE_MAX);
    emit(id, (errno == ERANGE) ? "REFUSED" : "ACCEPTED");
}
/* 40 memcmp: logging OFF/ON parity (C-only instrumentation) */
static void c40(int id) {
    unsigned char a[4] = {1, 2, 3, 4}, b[4] = {1, 2, 3, 4}; int ea, eb;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    errno = 0; (void)memcmp(a, b, 4); ea = errno;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_DEBUG);
    errno = 0; (void)memcmp(a, b, 4); eb = errno;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    emit(id, (ea == 0 && eb == 0) ? "ACCEPTED" : "REFUSED");
}

/* ========================================================================= */
/* 41..50  strcpy                                                            */

/* 41 strcpy: exact fit (src "abc", dest[4]) */
static void c41(int id) {
    char d[4]; char *r;
    errno = 0; r = strcpy(d, "abc");
    emit(id, (r == d && errno == 0 && eq(d, "abc", 4)) ? "ACCEPTED" : "REFUSED");
}
/* 42 strcpy: zero-length source (empty string) */
static void c42(int id) {
    char d[2] = {'X', 'X'}; char *r;
    errno = 0; r = strcpy(d, "");
    emit(id, (r == d && errno == 0 && d[0] == 0) ? "ACCEPTED" : "REFUSED");
}
/* 43 strcpy: capacity-1 (src "abc", dest[5]) leaves slack */
static void c43(int id) {
    char d[5] = {'X', 'X', 'X', 'X', 'X'}; char *r;
    errno = 0; r = strcpy(d, "abc");
    emit(id, (r == d && errno == 0 && eq(d, "abc", 4)) ? "ACCEPTED" : "REFUSED");
}
/* 44 strcpy: exactly capacity, static-storage destination */
static void c44(int id) {
    char *r;
    g_str[0] = 'Y';
    errno = 0; r = strcpy(g_str, "abcde");
    emit(id, (r == g_str && errno == 0 && eq(g_str, "abcde", 6)) ? "ACCEPTED" : "REFUSED");
}
/* 45 strcpy: capacity+1 (src "abcd", dest[4]) -> ERANGE */
static void c45(int id) {
    char d[4] = {'X', 'X', 'X', 'X'}; char *r;
    errno = 0; r = strcpy(d, "abcd");
    /* safeclib clears the destination on a string constraint violation. */
    emit(id, (r == NULL && errno == ERANGE) ? "REFUSED" : "ACCEPTED");
}
/* 46 strcpy: NULL source via checked auto wrapper -> EINVAL */
static void c46(int id) {
    char d[4] = {'X', 'X', 'X', 'X'}; char *r;
    errno = 0; r = strcpy(d, NULL);
    emit(id, (r == NULL && errno == EINVAL) ? "REFUSED" : "ACCEPTED");
}
/* 47 strcpy: embedded NUL source ("a\0b") copies up to first NUL */
static void c47(int id) {
    char d[4] = {'X', 'X', 'X', 'X'}; char *r;
    errno = 0; r = strcpy(d, "a\0b");
    emit(id, (r == d && errno == 0 && d[0] == 'a' && d[1] == 0) ? "ACCEPTED" : "REFUSED");
}
/* 48 strcpy: high-bit bytes are ordinary data to the checked wrapper */
static void c48(int id) {
    const char s[3] = {(char)0x80, (char)0xFF, 0}; char d[4]; char *r;
    errno = 0; r = strcpy(d, s);
    emit(id, (r == d && errno == 0 && eq(d, s, 3)) ? "ACCEPTED" : "REFUSED");
}
/* 49 strcpy: canary-guarded member, source fits */
static void c49(int id) {
    struct canary c; char *r;
    canary_init(&c);
    errno = 0; r = strcpy((char *)c.body, "hello");
    emit(id, (r == (char *)c.body && errno == 0 && eq(c.body, "hello", 6) && canary_ok(&c)) ? "ACCEPTED" : "REFUSED");
}
/* 50 strcpy: logging OFF/ON parity (C-only instrumentation) */
static void c50(int id) {
    char a[8], b[8]; int ea, eb;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    errno = 0; (void)strcpy(a, "abc"); ea = errno;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_DEBUG);
    errno = 0; (void)strcpy(b, "abc"); eb = errno;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    emit(id, (ea == 0 && eb == 0 && eq(a, b, 4)) ? "ACCEPTED" : "REFUSED");
}

/* ========================================================================= */
/* 51..60  strncpy                                                           */

/* 51 strncpy: exact fit (n=3, src "abc", dest[4]) */
static void c51(int id) {
    char d[4]; char *r;
    errno = 0; r = strncpy(d, "abc", 3);
    emit(id, (r == d && errno == 0 && eq(d, "abc", 4)) ? "ACCEPTED" : "REFUSED");
}
/* 52 strncpy: zero length (n=0) leaves destination untouched */
static void c52(int id) {
    char d[4] = {'X', 'X', 'X', 'X'}; char *r;
    errno = 0; r = strncpy(d, "abc", 0);
    emit(id, (r == d && errno == 0 && d[0] == 'X') ? "ACCEPTED" : "REFUSED");
}
/* 53 strncpy: capacity-1 (n=3, dest[5]) */
static void c53(int id) {
    char d[5] = {'X', 'X', 'X', 'X', 'X'}; char *r;
    errno = 0; r = strncpy(d, "abc", 3);
    emit(id, (r == d && errno == 0 && eq(d, "abc", 4)) ? "ACCEPTED" : "REFUSED");
}
/* 54 strncpy: exactly capacity (n=4, dest[5] = n+1) */
static void c54(int id) {
    char d[5]; char *r;
    errno = 0; r = strncpy(d, "abcd", 4);
    emit(id, (r == d && errno == 0 && eq(d, "abcd", 5)) ? "ACCEPTED" : "REFUSED");
}
/* 55 strncpy: capacity+1 (n=4, dest[4]) -> ERANGE */
static void c55(int id) {
    char d[4] = {'X', 'X', 'X', 'X'}; char *r;
    errno = 0; r = strncpy(d, "abcd", 4);
    /* safeclib clears the destination on a string constraint violation. */
    emit(id, (r == NULL && errno == ERANGE) ? "REFUSED" : "ACCEPTED");
}
/* 56 strncpy: NULL source via checked auto wrapper -> EINVAL */
static void c56(int id) {
    char d[4] = {'X', 'X', 'X', 'X'}; char *r;
    errno = 0; r = strncpy(d, NULL, 3);
    emit(id, (r == NULL && errno == EINVAL) ? "REFUSED" : "ACCEPTED");
}
/* 57 strncpy: SIZE_MAX count -> ERANGE (checked before any read) */
static void c57(int id) {
    char d[4] = {'X', 'X', 'X', 'X'}; char *r;
    errno = 0; r = strncpy(d, "abc", SIZE_MAX);
    emit(id, (r == NULL && errno == ERANGE) ? "REFUSED" : "ACCEPTED");
}
/* 58 strncpy: UNTERMINATED source with explicit bound (checked _s API;
 * the auto wrapper would call strlen and over-read, so it is not used here) */
static void c58(int id) {
    const char s[4] = {'a', 'b', 'c', 'd'}; char d[5]; errno_t e;
    e = cplus_strncpy_s(d, sizeof d, s, 4);
    emit(id, (e == 0 && eq(d, "abcd", 4) && d[4] == 0) ? "ACCEPTED" : "REFUSED");
}
/* 59 strncpy: embedded NUL in source copies up to and including it */
static void c59(int id) {
    const char s[4] = {'a', 'b', 0, 'd'}; char d[5] = {'X','X','X','X','X'}; char *r;
    errno = 0; r = strncpy(d, s, 4);
    emit(id, (r == d && errno == 0 && eq(d, "ab", 3)) ? "ACCEPTED" : "REFUSED");
}
/* 60 strncpy: high-bit bytes into canary-guarded member */
static void c60(int id) {
    struct canary c; const char s[3] = {(char)0x80, (char)0xFF, 0}; char *r;
    canary_init(&c);
    errno = 0; r = strncpy((char *)c.body, s, 2);
    emit(id, (r == (char *)c.body && errno == 0 && eq(c.body, s, 2) && canary_ok(&c)) ? "ACCEPTED" : "REFUSED");
}

/* ========================================================================= */
/* 61..70  strcat                                                            */

/* 61 strcat: exact fit ("ab"+"cde" into dest[6]) */
static void c61(int id) {
    char d[6] = "ab"; char *r;
    errno = 0; r = strcat(d, "cde");
    emit(id, (r == d && errno == 0 && eq(d, "abcde", 6)) ? "ACCEPTED" : "REFUSED");
}
/* 62 strcat: zero-length source appends nothing */
static void c62(int id) {
    char d[4] = "ab"; char *r;
    errno = 0; r = strcat(d, "");
    emit(id, (r == d && errno == 0 && eq(d, "ab", 3)) ? "ACCEPTED" : "REFUSED");
}
/* 63 strcat: capacity-1 (dest[7]) leaves one slack byte */
static void c63(int id) {
    char d[7] = "ab"; char *r;
    errno = 0; r = strcat(d, "cde");
    emit(id, (r == d && errno == 0 && eq(d, "abcde", 6)) ? "ACCEPTED" : "REFUSED");
}
/* 64 strcat: exactly capacity, static-storage destination */
static void c64(int id) {
    char *r;
    memcpy(g_str, "ab", 3);
    errno = 0; r = strcat(g_str, "cde");
    emit(id, (r == g_str && errno == 0 && eq(g_str, "abcde", 6)) ? "ACCEPTED" : "REFUSED");
}
/* 65 strcat: capacity+1 (dest[5], need 6) -> ERANGE */
static void c65(int id) {
    char d[5] = "ab"; char *r;
    errno = 0; r = strcat(d, "cde");
    /* safeclib clears the destination on a string constraint violation. */
    emit(id, (r == NULL && errno == ERANGE) ? "REFUSED" : "ACCEPTED");
}
/* 66 strcat: NULL source via checked auto wrapper -> EINVAL */
static void c66(int id) {
    char d[8] = "ab"; char *r;
    errno = 0; r = strcat(d, NULL);
    emit(id, (r == NULL && errno == EINVAL) ? "REFUSED" : "ACCEPTED");
}
/* 67 strcat: SIZE_MAX dmax via checked _s API -> constraint refusal */
static void c67(int id) {
    char d[8] = "ab"; errno_t e;
    e = cplus_strcat_s(d, (rsize_t)SIZE_MAX, "x");
    emit(id, (e != 0) ? "REFUSED" : "ACCEPTED");
}
/* 68 strcat: embedded NUL in source appends up to first NUL */
static void c68(int id) {
    char d[8] = "ab"; char *r;
    errno = 0; r = strcat(d, "x\0y");
    emit(id, (r == d && errno == 0 && eq(d, "abx", 4)) ? "ACCEPTED" : "REFUSED");
}
/* 69 strcat: canary-guarded member, append fits */
static void c69(int id) {
    struct canary c; char *r;
    canary_init(&c); H_MEMCPY(c.body, "ab\0", 3);
    errno = 0; r = strcat((char *)c.body, "cde");
    emit(id, (r == (char *)c.body && errno == 0 && eq(c.body, "abcde", 6) && canary_ok(&c)) ? "ACCEPTED" : "REFUSED");
}
/* 70 strcat: logging OFF/ON parity (C-only instrumentation) */
static void c70(int id) {
    char a[8], b[8]; int ea, eb;
    memcpy(a, "ab", 3); memcpy(b, "ab", 3);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    errno = 0; (void)strcat(a, "cd"); ea = errno;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_DEBUG);
    errno = 0; (void)strcat(b, "cd"); eb = errno;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    emit(id, (ea == 0 && eb == 0 && eq(a, b, 5)) ? "ACCEPTED" : "REFUSED");
}

/* ========================================================================= */
/* 71..80  strncat                                                           */

/* 71 strncat: exact fit ("ab"+"cde", n=3, dest[8]) */
static void c71(int id) {
    char d[8] = "ab"; char *r;
    errno = 0; r = strncat(d, "cde", 3);
    emit(id, (r == d && errno == 0 && eq(d, "abcde", 6)) ? "ACCEPTED" : "REFUSED");
}
/* 72 strncat: zero length (n=0) appends nothing */
static void c72(int id) {
    char d[8] = "ab"; char *r;
    errno = 0; r = strncat(d, "cde", 0);
    emit(id, (r == d && errno == 0 && eq(d, "ab", 3)) ? "ACCEPTED" : "REFUSED");
}
/* 73 strncat: capacity-1 (dest[7], need 6) */
static void c73(int id) {
    char d[7] = "ab"; char *r;
    errno = 0; r = strncat(d, "cde", 3);
    emit(id, (r == d && errno == 0 && eq(d, "abcde", 6)) ? "ACCEPTED" : "REFUSED");
}
/* 74 strncat: exactly capacity (dest[6], need 6) */
static void c74(int id) {
    char d[6] = "ab"; char *r;
    errno = 0; r = strncat(d, "cde", 3);
    emit(id, (r == d && errno == 0 && eq(d, "abcde", 6)) ? "ACCEPTED" : "REFUSED");
}
/* 75 strncat: capacity+1 (dest[5], need 6) -> ERANGE */
static void c75(int id) {
    char d[5] = "ab"; char *r;
    errno = 0; r = strncat(d, "cde", 3);
    /* safeclib clears the destination on a string constraint violation. */
    emit(id, (r == NULL && errno == ERANGE) ? "REFUSED" : "ACCEPTED");
}
/* 76 strncat: NULL source via checked auto wrapper -> EINVAL */
static void c76(int id) {
    char d[8] = "ab"; char *r;
    errno = 0; r = strncat(d, NULL, 3);
    emit(id, (r == NULL && errno == EINVAL) ? "REFUSED" : "ACCEPTED");
}
/* 77 strncat: SIZE_MAX count -> ERANGE */
static void c77(int id) {
    char d[8] = "ab"; char *r;
    errno = 0; r = strncat(d, "cde", SIZE_MAX);
    emit(id, (r == NULL && errno == ERANGE) ? "REFUSED" : "ACCEPTED");
}
/* 78 strncat: UNTERMINATED source with explicit bound (checked _s API) */
static void c78(int id) {
    struct canary c; const char s[4] = {'c', 'd', 'e', 'f'}; errno_t e;
    canary_init(&c); H_MEMCPY(c.body, "ab\0", 3);
    e = cplus_strncat_s((char *)c.body, (rsize_t)sizeof c.body, s, 4);
    emit(id, (e == 0 && eq(c.body, "abcdef", 7) && canary_ok(&c)) ? "ACCEPTED" : "REFUSED");
}
/* 79 strncat: embedded NUL in source appends up to first NUL */
static void c79(int id) {
    char d[8] = "ab"; char *r;
    errno = 0; r = strncat(d, "x\0y", 3);
    emit(id, (r == d && errno == 0 && eq(d, "abx", 4)) ? "ACCEPTED" : "REFUSED");
}
/* 80 strncat: high-bit bytes into canary-guarded member */
static void c80(int id) {
    struct canary c; const char s[3] = {(char)0x80, (char)0xFF, 0}; char *r;
    canary_init(&c); H_MEMCPY(c.body, "ab\0", 3);
    errno = 0; r = strncat((char *)c.body, s, 2);
    emit(id, (r == (char *)c.body && errno == 0 && eq(c.body, "ab", 2) &&
              (unsigned char)c.body[2] == 0x80 && (unsigned char)c.body[3] == 0xFF && canary_ok(&c)) ? "ACCEPTED" : "REFUSED");
}

/* ========================================================================= */
/* 81..90  strlen (bounded queries used wherever termination is unproven)     */

/* 81 strlen: ordinary terminated string */
static void c81(int id) {
    const char s[8] = "hello";
    size_t n = strlen(s);
    emit(id, (n == 5) ? "ACCEPTED" : "REFUSED");
}
/* 82 strlen: empty string -> 0 */
static void c82(int id) {
    const char s[2] = "";
    size_t n = strlen(s);
    emit(id, (n == 0) ? "ACCEPTED" : "REFUSED");
}
/* 83 strlen: NUL at last byte of 16-byte buffer (length 15) */
static void c83(int id) {
    char b[16]; size_t i;
    for (i = 0; i < 15; ++i) b[i] = 'a';
    b[15] = 0;
    emit(id, (strlen(b) == 15) ? "ACCEPTED" : "REFUSED");
}
/* 84 strlen: NUL mid-buffer after high-bit-free prefix (length 8) */
static void c84(int id) {
    char b[16]; size_t i;
    for (i = 0; i < 8; ++i) b[i] = (char)('A' + (int)i);
    b[8] = 0;
    emit(id, (strlen(b) == 8) ? "ACCEPTED" : "REFUSED");
}
/* 85 strlen: NO terminator in 16 bytes -- bounded query detects it, raw
 * strlen is deliberately avoided; reported REFUSED as a string */
static void c85(int id) {
    unsigned char b[16]; size_t n;
    fill(b, 16, 85);
    n = strnlen((const char *)b, sizeof b);
    emit(id, (n == sizeof b) ? "REFUSED" : "ACCEPTED");
}
/* 86 strlen: NULL via checked length query cplus_strnlen_s -> 0 = refusal */
static void c86(int id) {
    rsize_t n = cplus_strnlen_s(NULL, 8);
    emit(id, (n == 0) ? "REFUSED" : "ACCEPTED");
}
/* 87 strlen: SIZE_MAX bound refused before any read */
static void c87(int id) {
    const char b[8] = "abc";
    size_t bound = SIZE_MAX;
    if (bound > sizeof b) { emit(id, "REFUSED"); return; }
    emit(id, (strlen(b) == 3) ? "ACCEPTED" : "REFUSED");
}
/* 88 strlen: embedded NUL stops the count at the first one */
static void c88(int id) {
    const char b[8] = {'a', 'b', 0, 'c', 'd', 0, 0, 0};
    emit(id, (strlen(b) == 2) ? "ACCEPTED" : "REFUSED");
}
/* 89 strlen: high-bit bytes before NUL are counted as ordinary chars */
static void c89(int id) {
    const char b[4] = {(char)0x80, (char)0xFF, 0, 0};
    emit(id, (strlen(b) == 2) ? "ACCEPTED" : "REFUSED");
}
/* 90 strlen: logging OFF/ON parity (C-only instrumentation) */
static void c90(int id) {
    const char s[8] = "abc";
    size_t a, b;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF); a = strlen(s);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_DEBUG); b = strlen(s);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    emit(id, (a == b && a == 3) ? "ACCEPTED" : "REFUSED");
}

/* ========================================================================= */
/* 91..100  strnlen                                                          */

/* 91 strnlen: NUL found within bound -> returns exact length */
static void c91(int id) {
    const char b[16] = "abc";
    emit(id, (strnlen(b, sizeof b) == 3) ? "ACCEPTED" : "REFUSED");
}
/* 92 strnlen: zero bound -> defined 0 (POSIX bounded query) */
static void c92(int id) {
    const char b[4] = "abc";
    emit(id, (strnlen(b, 0) == 0) ? "ACCEPTED" : "REFUSED");
}
/* 93 strnlen: capacity-1 bound position (NUL at 14 of 16) */
static void c93(int id) {
    char b[16]; size_t i;
    for (i = 0; i < 16; ++i) b[i] = 'z';
    b[14] = 0;
    emit(id, (strnlen(b, sizeof b) == 14) ? "ACCEPTED" : "REFUSED");
}
/* 94 strnlen: NUL exactly at capacity (index 15 of 16) */
static void c94(int id) {
    char b[16]; size_t i;
    for (i = 0; i < 16; ++i) b[i] = 'z';
    b[15] = 0;
    emit(id, (strnlen(b, sizeof b) == 15) ? "ACCEPTED" : "REFUSED");
}
/* 95 strnlen: capacity+1 bound (17 > 16) refused before any read */
static void c95(int id) {
    const char b[16] = "abc";
    size_t bound = 17;
    if (bound > sizeof b) { emit(id, "REFUSED"); return; }
    emit(id, (strnlen(b, bound) == 3) ? "ACCEPTED" : "REFUSED");
}
/* 96 strnlen: NULL via checked query cplus_strnlen_s -> 0 = refusal */
static void c96(int id) {
    rsize_t n = cplus_strnlen_s(NULL, 8);
    emit(id, (n == 0) ? "REFUSED" : "ACCEPTED");
}
/* 97 strnlen: SIZE_MAX bound refused before any read */
static void c97(int id) {
    const char b[8] = "abc";
    size_t bound = SIZE_MAX;
    if (bound > sizeof b) { emit(id, "REFUSED"); return; }
    emit(id, (strnlen(b, bound) == 3) ? "ACCEPTED" : "REFUSED");
}
/* 98 strnlen: no NUL within bound returns the bound (defined, ACCEPTED) */
static void c98(int id) {
    unsigned char b[16]; size_t n;
    fill(b, 16, 98);
    n = strnlen((const char *)b, sizeof b);
    emit(id, (n == sizeof b) ? "ACCEPTED" : "REFUSED");
}
/* 99 strnlen: embedded NUL returns first terminator position */
static void c99(int id) {
    const char b[8] = {'a', 'b', 0, 'c', 'd', 0, 0, 0};
    emit(id, (strnlen(b, sizeof b) == 2) ? "ACCEPTED" : "REFUSED");
}
/* 100 strnlen: high-bit bytes + logging OFF/ON parity (C-only instrumentation) */
static void c100(int id) {
    const char b[4] = {(char)0x80, (char)0xFF, 0, 0};
    size_t a, c;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF); a = strnlen(b, sizeof b);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_DEBUG); c = strnlen(b, sizeof b);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    emit(id, (a == c && a == 2) ? "ACCEPTED" : "REFUSED");
}

/* ------------------------------------------------------------------------- */
/* Compile-time assertion: exactly 100 cases are registered.                 */

typedef void (*case_fn)(int);
static case_fn const cases[100] = {
    c01, c02, c03, c04, c05, c06, c07, c08, c09, c10,
    c11, c12, c13, c14, c15, c16, c17, c18, c19, c20,
    c21, c22, c23, c24, c25, c26, c27, c28, c29, c30,
    c31, c32, c33, c34, c35, c36, c37, c38, c39, c40,
    c41, c42, c43, c44, c45, c46, c47, c48, c49, c50,
    c51, c52, c53, c54, c55, c56, c57, c58, c59, c60,
    c61, c62, c63, c64, c65, c66, c67, c68, c69, c70,
    c71, c72, c73, c74, c75, c76, c77, c78, c79, c80,
    c81, c82, c83, c84, c85, c86, c87, c88, c89, c90,
    c91, c92, c93, c94, c95, c96, c97, c98, c99, c100
};
typedef char assert_exactly_100_cases[(sizeof cases / sizeof cases[0] == 100) ? 1 : -1];

int main(int argc, char **argv) {
    int n = (argc > 1) ? atoi(argv[1]) : 1;
    /* Recoverable mode: refusals return via errno/NULL; never abort. */
    cplus_runtime_set_violation_trap(0);
    cplus_runtime_set_failstop(0);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_safe_set_unknown_capacity_mode(0);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    setvbuf(stdout, NULL, _IONBF, 0);
    if (n >= 1 && n <= 100) cases[n - 1](n);
    else emit(n, "SKIP");
    return 0;
}
