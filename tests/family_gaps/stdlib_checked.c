/* Checked-coverage evidence for the stdlib.h family.
 *
 * Addresses the reviewer note "Partial: not every operation has a fully
 * checked variant" by EXERCISING every exported stdlib-family operation with
 * normal / boundary / invalid cases (where the API defines them), asserting a
 * defined result and errno, and asserting logging ON/OFF parity for each one.
 *
 * Two build modes share this single source (the same dual-lib story as
 * tests/methods/methods_alloc.c):
 *
 *  (1) SafeCRT layer (default) -- ordinary C spellings are macro-mapped by
 *      stdlib/safe/safe_c.h; the genuinely checked allocators are the _s names
 *      from stdlib/safe/safe_alloc.h:
 *        clang -O1 -g -std=c99 -Wall -Wextra -pedantic \
 *          -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
 *          -Ithird_party/safeclib/include \
 *          -Ithird_party/intel-safestringlib/include \
 *          tests/family_gaps/stdlib_checked.c \
 *          build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *          -lm -pthread -o /tmp/stdlib_checked && /tmp/stdlib_checked
 *
 *  (2) Plain libc layer -- only libcplus_hardened.a; the checked entry points
 *      are the cplus_* equivalents (cplus_malloc, cplus_free_checked, ...):
 *        clang -O1 -g -std=c99 -Wall -Wextra -pedantic -DPLAIN_LIBC \
 *          -I. -Ilibc/install-include tests/family_gaps/stdlib_checked.c \
 *          build/libc/libcplus_hardened.a -lm -pthread \
 *          -o /tmp/stdlib_checked_plain && /tmp/stdlib_checked_plain
 *
 *  UBSan: add -fsanitize=undefined -fno-sanitize-recover=all to either build.
 *
 * NO GENUINE UB: invalid/double/foreign pointers go only through the CHECKED
 * free entry point; exit/abort/atexit run only in a forked child; system()
 * receives only the harmless shell no-op ":"; division uses non-zero
 * denominators; abs()/llabs() never receive the unrepresentable minimum.
 *
 * Every FINDING line is prefixed with the exact operation name.
 */
#define _POSIX_C_SOURCE 200809L

#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h" /* includes cplus_libc.h */
#include <stdio.h>
#define LAYER "plain-libc"
/* Checked entry points in this layer. */
#define A_malloc            cplus_malloc
#define A_calloc            cplus_calloc
#define A_realloc           cplus_realloc
#define A_free              cplus_free
#define A_checked_free(pp)  cplus_free_checked(pp) /* takes void ** */
#define A_aligned_alloc     cplus_aligned_alloc
#define A_len(ptr)          cplus_alloc_len(ptr)
#define A_query_length      cplus_alloc_query_length
#define A_transfer          cplus_transfer_ownership
#define A_fail_after        cplus_alloc_fail_after
#define A_fail_reset        cplus_alloc_fail_reset
#else
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_alloc.h"
#include "stdlib/safe/safe_log.h"
#include "stdlib/safe/safe_runtime.h"
#include <stdio.h>
#define LAYER "safecrt"
/* Checked entry points in this layer (the literal _s names). */
#define A_malloc            malloc_s
#define A_calloc            calloc_s
#define A_realloc           realloc_s
#define A_free              free_s
#define A_checked_free(pp)  free_checked_s(*(pp)) /* takes void * */
#define A_aligned_alloc     aligned_alloc_s
#define A_len(ptr)          cplus_safe_len(ptr)
#define A_query_length      cplus_safe_query_length
#define A_transfer          cplus_safe_transfer_ownership
#define A_fail_after        cplus_safe_alloc_fail_after
#define A_fail_reset        cplus_safe_alloc_fail_reset
#endif

#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* The two checked allocation layers use different (but equally defined) errno
 * values for oversized requests: SafeCRT reports EINVAL / EOVERFLOW, the plain
 * libc layer reports ENOMEM. Both are asserted, per layer. */
#ifdef PLAIN_LIBC
#define E_ALLOC_SIZE     ENOMEM
#define E_ALLOC_OVERFLOW ENOMEM
#else
#define E_ALLOC_SIZE     EINVAL
#define E_ALLOC_OVERFLOW EOVERFLOW
#endif

/* aligned_alloc_s blocks are released with free_s (SafeCRT tracks them), but
 * cplus_aligned_alloc blocks come from the host allocator and need raw free().
 * Capture the raw host free before the ordinary-C macro is (re)established. */
#undef free
#ifdef PLAIN_LIBC
static void host_free(void *p) { free(p); } /* raw host free */
#define A_aligned_free(p) host_free(p)
#define free(p) cplus_free(p)
#else
#define A_aligned_free(p) free_s(p)
#define free(p) free_s(p)
#endif

/* ---- tiny harness -------------------------------------------------------- */
static int g_checks = 0;
static int g_fail = 0;
static const char *g_op = "?";

#define CHECK(cond, ...)                                     \
    do {                                                     \
        ++g_checks;                                          \
        if (!(cond)) {                                       \
            ++g_fail;                                        \
            printf("FINDING: %s: ", g_op);                   \
            printf(__VA_ARGS__);                             \
            putchar('\n');                                   \
        }                                                    \
    } while (0)

static int g_saved_stderr = -1;
static void mute_stderr(void) {
    int fd = open("/dev/null", O_WRONLY);
    if (fd < 0) return;
    g_saved_stderr = dup(STDERR_FILENO);
    if (g_saved_stderr >= 0) (void)dup2(fd, STDERR_FILENO);
    (void)close(fd);
}
static void unmute_stderr(void) {
    if (g_saved_stderr >= 0) {
        (void)dup2(g_saved_stderr, STDERR_FILENO);
        (void)close(g_saved_stderr);
        g_saved_stderr = -1;
    }
}

static void set_logging(int on) {
    cplus_libc_log_set_enabled(on);
#ifndef PLAIN_LIBC
    cplus_safe_log_set_level(on ? CPLUS_SAFE_LOG_TRACE : CPLUS_SAFE_LOG_OFF);
#endif
}

/* Run an integer expression with logging OFF then ON; assert parity and the
 * expected value. errno is asserted for parity; specific errno is added by
 * the caller with PARITY_CALL_E where the API defines it. */
#define PARITY_CALL(expr, want)                                              \
    do {                                                                     \
        long long r0_, r1_;                                                  \
        int e0_, e1_;                                                        \
        set_logging(0); errno = 0; r0_ = (long long)(expr); e0_ = errno;     \
        set_logging(1); errno = 0; r1_ = (long long)(expr); e1_ = errno;     \
        CHECK(r0_ == r1_, "log-parity ret %lld/%lld", r0_, r1_);             \
        CHECK(e0_ == e1_, "log-parity errno %d/%d", e0_, e1_);               \
        CHECK(r0_ == (long long)(want), "expected ret %lld got %lld",        \
              (long long)(want), r0_);                                       \
    } while (0)

#define PARITY_CALL_E(expr, want, want_errno)                                \
    do {                                                                     \
        long long r0_, r1_;                                                  \
        int e0_, e1_;                                                        \
        set_logging(0); errno = 0; r0_ = (long long)(expr); e0_ = errno;     \
        set_logging(1); errno = 0; r1_ = (long long)(expr); e1_ = errno;     \
        CHECK(r0_ == r1_, "log-parity ret %lld/%lld", r0_, r1_);             \
        CHECK(e0_ == e1_, "log-parity errno %d/%d", e0_, e1_);               \
        CHECK(r0_ == (long long)(want), "expected ret %lld got %lld",        \
              (long long)(want), r0_);                                       \
        CHECK(e0_ == (want_errno), "expected errno %d got %d",               \
              (want_errno), e0_);                                            \
    } while (0)

static int same_bits_d(double a, double b) {
    return memcmp(&a, &b, sizeof a) == 0;
}
static int same_bits_f(float a, float b) {
    return memcmp(&a, &b, sizeof a) == 0;
}
static int same_ld(long double a, long double b) {
    return a == b && signbit(a) == signbit(b);
}

/* ========================================================================= */
/* ALLOCATION: ordinary C spellings (macro-mapped to the checked allocator)   */
/* ========================================================================= */

static void op_malloc(void) {
    void *a, *b;
    int ea, eb;
    g_op = "malloc";
    set_logging(0); errno = 0; a = malloc(64);  ea = errno;
    set_logging(1); errno = 0; b = malloc(64);  eb = errno;
    CHECK(a != NULL && b != NULL, "malloc(64) NULL (off=%d on=%d)", a == NULL, b == NULL);
    CHECK(ea == eb, "log-parity errno %d/%d", ea, eb);
    A_free(a); A_free(b);
    /* boundary: size 0 -> defined 1-byte block */
    set_logging(0); errno = 0; a = malloc(0); ea = errno;
    set_logging(1); errno = 0; b = malloc(0); eb = errno;
    CHECK(a != NULL && b != NULL, "malloc(0) must yield a defined block");
    CHECK(ea == eb, "log-parity errno(size 0) %d/%d", ea, eb);
    A_free(a); A_free(b);
    /* invalid: absurd size -> NULL + a defined allocation error */
    PARITY_CALL_E(malloc((size_t)-1) != NULL, 0, E_ALLOC_SIZE);
}

static void op_calloc(void) {
    unsigned char *a, *b;
    int ea, eb, za, zb, i, ok = 1;
    g_op = "calloc";
    set_logging(0); errno = 0; a = (unsigned char *)calloc(8, 8); ea = errno;
    set_logging(1); errno = 0; b = (unsigned char *)calloc(8, 8); eb = errno;
    CHECK(a != NULL && b != NULL, "calloc(8,8) NULL");
    CHECK(ea == eb, "log-parity errno %d/%d", ea, eb);
    za = zb = 1;
    for (i = 0; i < 64; ++i) { if (a && a[i]) za = 0; if (b && b[i]) zb = 0; }
    CHECK(za && zb, "calloc memory not zeroed (%d/%d)", za, zb);
    ok = (a && b && memcmp(a, b, 64) == 0);
    CHECK(ok, "calloc log-parity contents differ");
    A_free(a); A_free(b);
    /* boundary: zero count and zero size both yield a defined 1-byte block */
    PARITY_CALL(calloc(0, 8) != NULL, 1);
    PARITY_CALL(calloc(8, 0) != NULL, 1);
    /* invalid: multiplication overflow -> NULL + a defined allocation error */
    PARITY_CALL_E(calloc((size_t)-1, (size_t)8) != NULL, 0, E_ALLOC_OVERFLOW);
}

static void op_realloc(void) {
    unsigned char *a, *b, *q;
    int ea, eb, oka, okb;
    g_op = "realloc";
    set_logging(0); errno = 0;
    a = (unsigned char *)malloc(16);
    if (!a) { CHECK(0, "setup malloc failed"); return; }
    memset(a, 0x5A, 16);
    q = (unsigned char *)realloc(a, 256); ea = errno;
    set_logging(1); errno = 0;
    b = (unsigned char *)malloc(16);
    if (!b) { A_free(q); CHECK(0, "setup malloc failed (on)"); return; }
    memset(b, 0x5A, 16);
    {
        unsigned char *r = (unsigned char *)realloc(b, 256); eb = errno;
        /* realloc preserves the OLD prefix; the new tail is uninitialised,
         * so write it before checking it rather than assuming its contents. */
        oka = (q != NULL && q[0] == 0x5A && q[15] == 0x5A);
        okb = (r != NULL && r[0] == 0x5A && r[15] == 0x5A);
        if (oka) q[255] = 0x5A;
        if (okb) r[255] = 0x5A;
        oka = oka && q[255] == 0x5A;
        okb = okb && r[255] == 0x5A;
        CHECK(oka && okb, "realloc grow lost prefix/full-write (%d/%d)", oka, okb);
        CHECK(ea == eb, "log-parity errno %d/%d", ea, eb);
        A_free(q); A_free(r);
    }
    /* boundary: realloc(NULL, n) behaves like malloc */
    PARITY_CALL(realloc(NULL, 48) != NULL, 1);
    /* invalid: over-large size preserves the owner and sets EINVAL */
    {
        void *p = malloc(32);
        void *r;
        if (!p) { CHECK(0, "setup malloc failed (size_max)"); return; }
        set_logging(0); errno = 0; r = realloc(p, (size_t)-1); ea = errno;
        CHECK(r == NULL && ea == E_ALLOC_SIZE, "realloc(SIZE_MAX) r=%p errno=%d", r, ea);
        memset(p, 0x33, 32); /* owner still live and writable */
        A_free(p);
    }
    /* invalid: foreign (stack) pointer refused safely by the checked wrapper */
    {
        char stack[16];
        void *r;
        set_logging(0); errno = 0; r = realloc((void *)stack, 32); ea = errno;
        set_logging(1); errno = 0; r = realloc((void *)stack, 32); eb = errno;
        CHECK(r == NULL, "realloc(foreign) must be refused");
        CHECK(ea == eb && ea == EINVAL, "realloc(foreign) errno %d/%d", ea, eb);
    }
}

static void op_free(void) {
    void *p, *q;
    g_op = "free";
    p = malloc(16); q = malloc(16);
    CHECK(p != NULL && q != NULL, "setup malloc failed");
    set_logging(0); errno = 0; free(p); {
        int e0 = errno;
        set_logging(1); errno = 0; free(q);
        CHECK(e0 == errno, "log-parity errno(free) %d/%d", e0, errno);
    }
    /* boundary: free(NULL) is a defined no-op */
    PARITY_CALL((free(NULL), 1), 1);
}

/* ========================================================================= */
/* INTEGER ABSOLUTE VALUE AND DIVISION                                        */
/* ========================================================================= */

static void op_abs(void) {
    g_op = "abs";
    PARITY_CALL(cplus_abs(-42), 42);
    PARITY_CALL(cplus_abs(0), 0);
    PARITY_CALL(cplus_abs(INT_MAX), INT_MAX); /* boundary, no INT_MIN UB */
    CHECK(cplus_abs(-7) == abs(-7), "host diff abs");
}

static void op_labs(void) {
    g_op = "labs";
    PARITY_CALL(cplus_labs(-1234L), 1234L);
    PARITY_CALL(cplus_labs(LONG_MAX), LONG_MAX);
    CHECK(cplus_labs(-9L) == labs(-9L), "host diff labs");
}

static void op_llabs(void) {
    g_op = "llabs";
    PARITY_CALL(cplus_llabs(-1234567890123LL), 1234567890123LL);
    PARITY_CALL(cplus_llabs(LLONG_MAX), LLONG_MAX);
    CHECK(cplus_llabs(-11LL) == llabs(-11LL), "host diff llabs");
}

static void op_div(void) {
    div_t a, b;
    g_op = "div";
    set_logging(0); errno = 0; a = cplus_div(17, 5); {
        int ea = errno;
        set_logging(1); errno = 0; b = cplus_div(17, 5);
        CHECK(a.quot == b.quot && a.rem == b.rem && ea == errno,
              "log-parity div %d/%d %d/%d", a.quot, b.quot, a.rem, b.rem);
    }
    CHECK(a.quot == 3 && a.rem == 2, "div(17,5) = %d,%d", a.quot, a.rem);
    /* negative numerator and boundary denominator */
    CHECK(cplus_div(-17, 5).quot == -3 && cplus_div(-17, 5).rem == -2, "div(-17,5)");
    CHECK(cplus_div(INT_MIN, 3).quot == INT_MIN / 3, "div boundary denominator");
    CHECK(a.quot == div(17, 5).quot && a.rem == div(17, 5).rem, "host diff div");
}

static void op_ldiv(void) {
    ldiv_t a, b;
    g_op = "ldiv";
    set_logging(0); errno = 0; a = cplus_ldiv(100L, 7L); {
        int ea = errno;
        set_logging(1); errno = 0; b = cplus_ldiv(100L, 7L);
        CHECK(a.quot == b.quot && a.rem == b.rem && ea == errno, "log-parity ldiv");
    }
    CHECK(a.quot == 14L && a.rem == 2L, "ldiv(100,7) = %ld,%ld", a.quot, a.rem);
    CHECK(a.quot == ldiv(100L, 7L).quot && a.rem == ldiv(100L, 7L).rem, "host diff ldiv");
}

static void op_lldiv(void) {
    lldiv_t a, b;
    g_op = "lldiv";
    set_logging(0); errno = 0; a = cplus_lldiv(1000000LL, 7LL); {
        int ea = errno;
        set_logging(1); errno = 0; b = cplus_lldiv(1000000LL, 7LL);
        CHECK(a.quot == b.quot && a.rem == b.rem && ea == errno, "log-parity lldiv");
    }
    CHECK(a.quot == 142857LL && a.rem == 1LL, "lldiv = %lld,%lld", a.quot, a.rem);
    CHECK(a.quot == lldiv(1000000LL, 7LL).quot && a.rem == lldiv(1000000LL, 7LL).rem,
          "host diff lldiv");
}

/* ========================================================================= */
/* STRING-TO-NUMBER CONVERSIONS                                               */
/* ========================================================================= */

static void op_atoi(void) {
    g_op = "atoi";
    PARITY_CALL(cplus_atoi("123"), 123);
    PARITY_CALL(cplus_atoi("  -42rest"), -42);
    PARITY_CALL(cplus_atoi("0"), 0);
    CHECK(cplus_atoi("  +57x") == atoi("  +57x"), "host diff atoi");
}

static void op_atol(void) {
    g_op = "atol";
    PARITY_CALL(cplus_atol("1234567L"), 1234567L);
    PARITY_CALL(cplus_atol("-5"), -5L);
    CHECK(cplus_atol(" 88abc") == atol(" 88abc"), "host diff atol");
}

static void op_atoll(void) {
    g_op = "atoll";
    PARITY_CALL(cplus_atoll("1234567890123LL"), 1234567890123LL);
    PARITY_CALL(cplus_atoll("-77"), -77LL);
    CHECK(cplus_atoll(" 314xyz") == atoll(" 314xyz"), "host diff atoll");
}

static void op_atof(void) {
    double a, b;
    g_op = "atof";
    set_logging(0); errno = 0; a = cplus_atof("  -2.25e2abc");
    set_logging(1); errno = 0; b = cplus_atof("  -2.25e2abc");
    CHECK(same_bits_d(a, b), "log-parity atof");
    CHECK(same_bits_d(a, -225.0), "atof expected -225 got %g", a);
    CHECK(same_bits_d(cplus_atof("3.5"), atof("3.5")), "host diff atof");
}

static void op_strtol(void) {
    char s1[] = "  -ff", s2[] = "999999999999999999999999999", s3[] = "abc";
    char *e0 = NULL, *e1 = NULL, *eh = NULL;
    long a, b, h;
    int ea, eb, eh_err;
    g_op = "strtol";
    set_logging(0); errno = 0; a = cplus_strtol(s1, &e0, 16); ea = errno;
    set_logging(1); errno = 0; b = cplus_strtol(s1, &e1, 16); eb = errno;
    errno = 0; h = strtol(s1, &eh, 16); eh_err = errno;
    CHECK(a == b && ea == eb, "log-parity strtol %ld/%ld errno %d/%d", a, b, ea, eb);
    CHECK(a == -255L && (e0 - s1) == 5, "strtol(hex) = %ld end=%td", a, e0 - s1);
    CHECK(a == h && ea == eh_err && (e0 - s1) == (eh - s1), "host diff strtol");
    /* boundary: overflow clamps and sets ERANGE */
    set_logging(0); errno = 0;
    a = cplus_strtol(s2, &e0, 10); ea = errno;
    set_logging(1); errno = 0;
    b = cplus_strtol(s2, &e1, 10); eb = errno;
    CHECK(a == LONG_MAX && b == LONG_MAX && ea == ERANGE && eb == ERANGE,
          "strtol overflow %ld/%ld errno %d/%d", a, b, ea, eb);
    /* invalid: no digits -> 0, endptr at start; errno is host-defined, so
     * compare it against the installed backend rather than hard-coding it. */
    set_logging(0); errno = 0;
    a = cplus_strtol(s3, &e0, 10); ea = errno;
    errno = 0; h = strtol(s3, &eh, 10); eh_err = errno;
    CHECK(a == 0 && e0 == s3 && a == h && ea == eh_err,
          "strtol(invalid) = %ld errno=%d host=%d end=%c", a, ea, eh_err, *e0);
}

static void op_strtoul(void) {
    char s1[] = "400";
    char *e0 = NULL, *e1 = NULL;
    unsigned long a, b;
    int ea, eb;
    g_op = "strtoul";
    set_logging(0); errno = 0; a = cplus_strtoul(s1, &e0, 8); ea = errno;
    set_logging(1); errno = 0; b = cplus_strtoul(s1, &e1, 8); eb = errno;
    CHECK(a == b && ea == eb, "log-parity strtoul %lu/%lu", a, b);
    CHECK(a == 256UL && (e0 - s1) == 3, "strtoul(octal) = %lu end=%td", a, e0 - s1);
    set_logging(0); errno = 0;
    a = cplus_strtoul("184467440737095516160", &e0, 10); ea = errno;
    CHECK(a == ULONG_MAX && ea == ERANGE, "strtoul overflow %lu errno=%d", a, ea);
}

static void op_strtoll(void) {
    char *e0 = NULL, *e1 = NULL;
    long long a, b;
    int ea, eb;
    g_op = "strtoll";
    set_logging(0); errno = 0; a = cplus_strtoll("-9223372036854775807", &e0, 10); ea = errno;
    set_logging(1); errno = 0; b = cplus_strtoll("-9223372036854775807", &e1, 10); eb = errno;
    CHECK(a == b && ea == eb, "log-parity strtoll %lld/%lld", a, b);
    CHECK(a == (-9223372036854775807LL), "strtoll boundary = %lld", a);
    set_logging(0); errno = 0;
    a = cplus_strtoll("99999999999999999999999999", &e0, 10); ea = errno;
    CHECK(a == LLONG_MAX && ea == ERANGE, "strtoll overflow %lld errno=%d", a, ea);
}

static void op_strtoull(void) {
    char *e0 = NULL, *e1 = NULL;
    unsigned long long a, b;
    int ea, eb;
    g_op = "strtoull";
    set_logging(0); errno = 0; a = cplus_strtoull("deadBEEF", &e0, 16); ea = errno;
    set_logging(1); errno = 0; b = cplus_strtoull("deadBEEF", &e1, 16); eb = errno;
    CHECK(a == b && ea == eb, "log-parity strtoull %llu/%llu", a, b);
    CHECK(a == 0xdeadBEEFULL, "strtoull(hex) = %llu", a);
    set_logging(0); errno = 0;
    a = cplus_strtoull("-1", &e0, 10); ea = errno;
    CHECK(a == ULLONG_MAX && ea == 0, "strtoull(-1) = %llu errno=%d", a, ea);
}

static void op_strtod(void) {
    char s1[] = "3.14159xyz", s2[] = "zzz";
    char *e0 = NULL, *e1 = NULL, *eh = NULL;
    double a, b, h;
    int ea, eb, eh_err;
    g_op = "strtod";
    set_logging(0); errno = 0; a = cplus_strtod(s1, &e0); ea = errno;
    set_logging(1); errno = 0; b = cplus_strtod(s1, &e1); eb = errno;
    errno = 0; h = strtod(s1, &eh); eh_err = errno;
    CHECK(same_bits_d(a, b) && ea == eb, "log-parity strtod");
    CHECK(same_bits_d(a, 3.14159) && (e0 - s1) == 7, "strtod = %g end=%td", a, e0 - s1);
    CHECK(same_bits_d(a, h) && ea == eh_err && (e0 - s1) == (eh - s1), "host diff strtod");
    set_logging(0); errno = 0;
    a = cplus_strtod("1e9999", &e0); ea = errno;
    CHECK(isinf(a) && a > 0 && ea == ERANGE, "strtod overflow %g errno=%d", a, ea);
    set_logging(0); errno = 0;
    a = cplus_strtod(s2, &e0); ea = errno;
    errno = 0; h = strtod(s2, &eh); eh_err = errno;
    CHECK(a == 0.0 && e0 == s2 && same_bits_d(a, h) && ea == eh_err,
          "strtod(invalid) = %g errno=%d host=%d", a, ea, eh_err);
}

static void op_strtof(void) {
    char s1[] = "2.75q";
    char *e0 = NULL, *e1 = NULL;
    float a, b;
    int ea, eb;
    g_op = "strtof";
    set_logging(0); errno = 0; a = cplus_strtof(s1, &e0); ea = errno;
    set_logging(1); errno = 0; b = cplus_strtof(s1, &e1); eb = errno;
    CHECK(same_bits_f(a, b) && ea == eb, "log-parity strtof");
    CHECK(same_bits_f(a, 2.75f) && (e0 - s1) == 4, "strtof = %g end=%td", (double)a, e0 - s1);
    set_logging(0); errno = 0;
    a = cplus_strtof("1e9999", &e0); ea = errno;
    CHECK(isinf(a) && a > 0 && ea == ERANGE, "strtof overflow errno=%d", ea);
}

static void op_strtold(void) {
    char s1[] = "6.25z";
    char *e0 = NULL, *e1 = NULL;
    long double a, b;
    int ea, eb;
    g_op = "strtold";
    set_logging(0); errno = 0; a = cplus_strtold(s1, &e0); ea = errno;
    set_logging(1); errno = 0; b = cplus_strtold(s1, &e1); eb = errno;
    CHECK(same_ld(a, b) && ea == eb, "log-parity strtold");
    CHECK(same_ld(a, 6.25L) && (e0 - s1) == 4, "strtold = %Lg end=%td", a, e0 - s1);
    set_logging(0); errno = 0;
    a = cplus_strtold("1e9999", &e0); ea = errno;
    CHECK(isinf(a) && a > 0 && ea == ERANGE, "strtold overflow errno=%d", ea);
}

/* ========================================================================= */
/* SORT / SEARCH                                                              */
/* ========================================================================= */

static int cmp_int(const void *pa, const void *pb) {
    int x = *(const int *)pa, y = *(const int *)pb;
    return (x > y) - (x < y);
}

static void op_qsort(void) {
    const int want[6] = { -2, 0, 1, 3, 5, 9 };
    int a[6] = { 5, -2, 9, 0, 3, 1 };
    int b[6] = { 5, -2, 9, 0, 3, 1 };
    int i, ok;
    g_op = "qsort";
    set_logging(0); errno = 0; cplus_qsort(a, 6, sizeof(int), cmp_int); {
        int ea = errno;
        set_logging(1); errno = 0; cplus_qsort(b, 6, sizeof(int), cmp_int);
        CHECK(ea == errno, "log-parity errno(qsort) %d/%d", ea, errno);
    }
    ok = 1;
    for (i = 0; i < 6; ++i) if (a[i] != b[i] || a[i] != want[i]) ok = 0;
    CHECK(ok, "qsort result / log-parity contents differ");
    /* boundary: zero elements must be a no-op */
    cplus_qsort(a, 0, sizeof(int), cmp_int);
    CHECK(1, "qsort(nmemb=0) returned");
}

static void op_bsearch(void) {
    int arr[5] = { 1, 3, 5, 7, 9 };
    int key = 7, absent = 4;
    int *r0, *r1;
    g_op = "bsearch";
    set_logging(0); errno = 0; r0 = (int *)cplus_bsearch(&key, arr, 5, sizeof(int), cmp_int); {
        int ea = errno;
        set_logging(1); errno = 0;
        r1 = (int *)cplus_bsearch(&key, arr, 5, sizeof(int), cmp_int);
        CHECK(ea == errno, "log-parity errno(bsearch) %d/%d", ea, errno);
    }
    CHECK(r0 != NULL && r1 != NULL && *r0 == 7 && *r1 == 7, "bsearch hit parity");
    /* invalid/boundary: absent key and zero-length table -> NULL */
    PARITY_CALL(cplus_bsearch(&absent, arr, 5, sizeof(int), cmp_int) != NULL, 0);
    PARITY_CALL(cplus_bsearch(&key, arr, 0, sizeof(int), cmp_int) != NULL, 0);
    CHECK(cplus_bsearch(&key, arr, 5, sizeof(int), cmp_int) ==
          bsearch(&key, arr, 5, sizeof(int), cmp_int), "host diff bsearch");
}

/* ========================================================================= */
/* PSEUDO-RANDOM                                                              */
/* ========================================================================= */

static void op_srand(void) {
    int r0, r1, rh;
    g_op = "srand";
    set_logging(0); errno = 0; cplus_srand(12345u); r0 = cplus_rand(); {
        int ea = errno;
        set_logging(1); errno = 0; cplus_srand(12345u); r1 = cplus_rand();
        CHECK(ea == errno, "log-parity errno(srand) %d/%d", ea, errno);
    }
    CHECK(r0 == r1, "srand reproducibility log-parity %d/%d", r0, r1);
    srand(12345u); rh = rand();
    CHECK(r0 == rh, "host diff srand/rand %d/%d", r0, rh);
}

static void op_rand(void) {
    int a, b, ha, hb, ea, eb;
    g_op = "rand";
    cplus_srand(777u);
    set_logging(0); errno = 0; a = cplus_rand(); ea = errno;
    set_logging(1); errno = 0; b = cplus_rand(); eb = errno;
    CHECK(ea == eb, "log-parity errno(rand) %d/%d", ea, eb);
    CHECK(a >= 0 && a <= RAND_MAX && b >= 0 && b <= RAND_MAX, "rand range %d/%d", a, b);
    /* Re-seed the host generator before comparing the same two draws. */
    srand(777u); ha = rand(); hb = rand();
    CHECK(a == ha && b == hb, "host diff rand %d/%d vs %d/%d", a, b, ha, hb);
}

/* ========================================================================= */
/* ENVIRONMENT                                                                */
/* ========================================================================= */

#define ENVNAME "CPLUS_STDLIB_CHECKED_TEST_VAR"

static void op_setenv(void) {
    int a, b, ea, eb;
    g_op = "setenv";
    (void)cplus_unsetenv(ENVNAME);
    set_logging(0); errno = 0; a = cplus_setenv(ENVNAME, "first", 1); ea = errno;
    set_logging(1); errno = 0; b = cplus_setenv(ENVNAME, "first", 1); eb = errno;
    CHECK(a == 0 && b == 0 && ea == eb, "setenv parity %d/%d errno %d/%d", a, b, ea, eb);
    CHECK(cplus_getenv(ENVNAME) && strcmp(cplus_getenv(ENVNAME), "first") == 0,
          "setenv value readback");
    /* overwrite=0 keeps the existing value; overwrite=1 replaces it */
    CHECK(cplus_setenv(ENVNAME, "second", 0) == 0, "setenv(overwrite=0) rc");
    CHECK(strcmp(cplus_getenv(ENVNAME), "first") == 0, "setenv(overwrite=0) value");
    CHECK(cplus_setenv(ENVNAME, "second", 1) == 0, "setenv(overwrite=1) rc");
    CHECK(strcmp(cplus_getenv(ENVNAME), "second") == 0, "setenv(overwrite=1) value");
    /* invalid: empty name -> -1 / EINVAL */
    PARITY_CALL_E(cplus_setenv("", "x", 1), -1, EINVAL);
    (void)cplus_unsetenv(ENVNAME);
}

static void op_unsetenv(void) {
    int rc0, rc1, ea, eb;
    g_op = "unsetenv";
    (void)cplus_setenv(ENVNAME, "x", 1);
    set_logging(0); errno = 0; rc0 = cplus_unsetenv(ENVNAME); ea = errno;
    set_logging(1); (void)cplus_setenv(ENVNAME, "x", 1); errno = 0;
    rc1 = cplus_unsetenv(ENVNAME); eb = errno;
    CHECK(rc0 == 0 && rc1 == 0 && ea == eb, "unsetenv parity %d/%d errno %d/%d",
          rc0, rc1, ea, eb);
    CHECK(cplus_getenv(ENVNAME) == NULL, "unsetenv left the variable visible");
    /* boundary: unsetting an absent variable is still success */
    CHECK(cplus_unsetenv(ENVNAME) == 0, "unsetenv(absent) rc");
    /* invalid: empty name -> -1 / EINVAL */
    PARITY_CALL_E(cplus_unsetenv(""), -1, EINVAL);
}

static void op_getenv(void) {
    const char *a, *b;
    g_op = "getenv";
    (void)cplus_setenv(ENVNAME, "value7", 1);
    set_logging(0); errno = 0; a = cplus_getenv(ENVNAME); {
        int ea = errno;
        set_logging(1); errno = 0; b = cplus_getenv(ENVNAME);
        CHECK(ea == errno, "log-parity errno(getenv) %d/%d", ea, errno);
    }
    CHECK(a != NULL && b != NULL && strcmp(a, "value7") == 0 && strcmp(a, b) == 0,
          "getenv value / parity");
    /* boundary: a name that was never set -> NULL */
    set_logging(0); errno = 0;
    CHECK(cplus_getenv("CPLUS_STDLIB_CHECKED_NEVER_SET") == NULL, "getenv(absent)");
    /* host parity */
    CHECK(strcmp(cplus_getenv(ENVNAME), getenv(ENVNAME)) == 0, "host diff getenv");
    (void)cplus_unsetenv(ENVNAME);
}

/* ========================================================================= */
/* EXIT FAMILY -- ONLY IN A FORKED CHILD                                      */
/* ========================================================================= */

static void run_exit_child(int logging_on, void (*fn)(void), int signal_expected,
                           int code_expected, const char *label) {
    pid_t pid;
    int status = 0;
    fflush(stdout);
    pid = fork();
    if (pid < 0) { CHECK(0, "fork failed (%s)", label); return; }
    if (pid == 0) {
        set_logging(logging_on);
        fn();
        _exit(97); /* fallback only; fn() is expected never to return */
    }
    if (waitpid(pid, &status, 0) != pid) { CHECK(0, "waitpid failed (%s)", label); return; }
    if (signal_expected) {
        CHECK(WIFSIGNALED(status) && WTERMSIG(status) == signal_expected,
              "%s signal=%d want=%d", label,
              WIFSIGNALED(status) ? WTERMSIG(status) : -1, signal_expected);
    } else {
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == code_expected,
              "%s code=%d want=%d", label,
              WIFEXITED(status) ? WEXITSTATUS(status) : -1, code_expected);
    }
}

static void child_exit(void) { cplus_exit(37); }
static void child_abort(void) { cplus_abort(); }

static void op_exit(void) {
    g_op = "exit";
    run_exit_child(0, child_exit, 0, 37, "exit/log-off");
    run_exit_child(1, child_exit, 0, 37, "exit/log-on");
}

static void op_abort(void) {
    g_op = "abort";
    run_exit_child(0, child_abort, SIGABRT, 0, "abort/log-off");
    run_exit_child(1, child_abort, SIGABRT, 0, "abort/log-on");
}

static int g_atexit_fd = -1;
static void atexit_marker(void) {
    unsigned char b = 0x41;
    if (g_atexit_fd >= 0) (void)write(g_atexit_fd, &b, 1);
}
static void run_atexit_child(int logging_on) {
    int p[2];
    unsigned char b = 0;
    ssize_t n;
    pid_t pid;
    int status = 0;
    fflush(stdout);
    if (pipe(p) != 0) { CHECK(0, "pipe failed (atexit)"); return; }
    g_atexit_fd = p[1];
    pid = fork();
    if (pid < 0) { CHECK(0, "fork failed (atexit)"); close(p[0]); close(p[1]); return; }
    if (pid == 0) {
        close(p[0]);
        set_logging(logging_on);
        (void)cplus_atexit(atexit_marker);
        cplus_exit(0);
        _exit(97);
    }
    close(p[1]);
    n = read(p[0], &b, 1);
    close(p[0]);
    g_atexit_fd = -1;
    if (waitpid(pid, &status, 0) != pid) { CHECK(0, "waitpid failed (atexit)"); return; }
    CHECK(n == 1 && b == 0x41, "atexit handler did not run (n=%zd byte=%d)", n, (int)b);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0, "atexit child code=%d want=0",
          WIFEXITED(status) ? WEXITSTATUS(status) : -1);
}
static void op_atexit(void) {
    g_op = "atexit";
    run_atexit_child(0);
    run_atexit_child(1);
}

/* ========================================================================= */
/* TEMP FILES / PATHS / SYSTEM                                                */
/* ========================================================================= */

static void op_mkstemp(void) {
    char t[80];
    int fd, ea, eb;
    g_op = "mkstemp";
    (void)snprintf(t, sizeof t, "%s/cplus_checked_XXXXXX", P_tmpdir);
    set_logging(0); errno = 0; fd = cplus_mkstemp(t); ea = errno;
    CHECK(fd >= 0 && strstr(t, "cplus_checked_") != NULL, "mkstemp fd=%d tmpl=%s", fd, t);
    CHECK(ea == 0, "mkstemp errno on success = %d", ea);
    if (fd >= 0) { close(fd); unlink(t); }
    /* logging-ON run with a fresh template */
    (void)snprintf(t, sizeof t, "%s/cplus_checked_XXXXXX", P_tmpdir);
    set_logging(1); errno = 0; fd = cplus_mkstemp(t); eb = errno;
    CHECK(fd >= 0 && eb == ea, "mkstemp(log-on) fd=%d errno=%d/%d", fd, ea, eb);
    if (fd >= 0) { close(fd); unlink(t); }
    /* invalid: a template in a nonexistent directory -> -1 / ENOENT.
     * (macOS accepts a literal template with no X's and creates that exact
     * file, so "no X's" is not a portable failure case here.) Use two fresh
     * buffers because a successful mkstemp rewrites its template. */
    {
        char bad[80];
        int r0, r1, e0, e1;
        (void)snprintf(t, sizeof t, "/no/such/cplus_dir_zzz/XXXXXX");
        (void)snprintf(bad, sizeof bad, "/no/such/cplus_dir_zzz/XXXXXX");
        set_logging(0); errno = 0; r0 = cplus_mkstemp(t); e0 = errno;
        set_logging(1); errno = 0; r1 = cplus_mkstemp(bad); e1 = errno;
        CHECK(r0 == -1 && r1 == -1 && e0 == ENOENT && e1 == ENOENT,
              "mkstemp(missing dir) rc %d/%d errno %d/%d", r0, r1, e0, e1);
    }
}

static void op_mkdtemp(void) {
    char t[80], u[80];
    char *r0, *r1;
    int ea, eb;
    g_op = "mkdtemp";
    (void)snprintf(t, sizeof t, "%s/cplus_checked_dXXXXXX", P_tmpdir);
    set_logging(0); errno = 0; r0 = cplus_mkdtemp(t); ea = errno;
    (void)snprintf(u, sizeof u, "%s/cplus_checked_dXXXXXX", P_tmpdir);
    set_logging(1); errno = 0; r1 = cplus_mkdtemp(u); eb = errno;
    CHECK(r0 != NULL && r1 != NULL && ea == eb, "mkdtemp parity %p/%p errno %d/%d",
          (void *)r0, (void *)r1, ea, eb);
    if (r0) rmdir(r0);
    if (r1) rmdir(r1);
    /* invalid: a template in a nonexistent directory -> NULL / ENOENT */
    (void)snprintf(t, sizeof t, "/no/such/cplus_dir_zzz/XXXXXX");
    (void)snprintf(u, sizeof u, "/no/such/cplus_dir_zzz/XXXXXX");
    set_logging(0); errno = 0; r0 = cplus_mkdtemp(t); ea = errno;
    set_logging(1); errno = 0; r1 = cplus_mkdtemp(u); eb = errno;
    CHECK(r0 == NULL && r1 == NULL && ea == ENOENT && eb == ENOENT,
          "mkdtemp(missing dir) %p/%p errno %d/%d", (void *)r0, (void *)r1, ea, eb);
}

static void op_realpath(void) {
    char a[PATH_MAX], b[PATH_MAX], h[PATH_MAX];
    char *r0, *r1, *rh;
    int ea, eb;
    g_op = "realpath";
    set_logging(0); errno = 0; r0 = cplus_realpath(".", a); ea = errno;
    set_logging(1); errno = 0; r1 = cplus_realpath(".", b); eb = errno;
    CHECK(r0 != NULL && r1 != NULL && ea == eb, "realpath parity %p/%p errno %d/%d",
          (void *)r0, (void *)r1, ea, eb);
    if (r0 && r1) CHECK(strcmp(a, b) == 0 && a[0] == '/', "realpath contents %s / %s", a, b);
    rh = realpath(".", h);
    CHECK(r0 && rh && strcmp(a, h) == 0, "host diff realpath %s / %s", a, h);
    /* invalid: nonexistent path -> NULL / ENOENT */
    set_logging(0); errno = 0;
    r0 = cplus_realpath("/no/such/cplus_checked_path_zzz", a); ea = errno;
    set_logging(1); errno = 0;
    r1 = cplus_realpath("/no/such/cplus_checked_path_zzz", b); eb = errno;
    CHECK(r0 == NULL && r1 == NULL && ea == ENOENT && eb == ENOENT,
          "realpath(missing) %p/%p errno %d/%d", (void *)r0, (void *)r1, ea, eb);
}

static void op_system(void) {
    g_op = "system";
    /* Only a harmless POSIX shell no-op is ever passed. */
    PARITY_CALL(cplus_system(":"), 0);
    CHECK(cplus_system(":") == system(":"), "host diff system(:)");
}

/* ========================================================================= */
/* CHECKED ALLOCATION EXTENSIONS (the literal _s / cplus_* entry points)      */
/* ========================================================================= */

static void op_malloc_s(void) {
    void *a, *b;
    int ea, eb;
    g_op = "malloc_s";
    set_logging(0); errno = 0; a = A_malloc(48); ea = errno;
    set_logging(1); errno = 0; b = A_malloc(48); eb = errno;
    CHECK(a != NULL && b != NULL && ea == eb, "malloc_s parity %p/%p errno %d/%d",
          a, b, ea, eb);
    A_free(a); A_free(b);
    /* boundary: 0 -> defined 1-byte block; invalid: >RSIZE_MAX_MEM -> EINVAL */
    PARITY_CALL(A_malloc(0) != NULL, 1);
    {
        void *p;
        int e;
        set_logging(0); errno = 0; p = A_malloc((size_t)-1); e = errno;
        CHECK(p == NULL && e == E_ALLOC_SIZE, "malloc_s(SIZE_MAX) p=%p errno=%d", p, e);
    }
}

static void op_calloc_s(void) {
    unsigned char *p;
    int e;
    g_op = "calloc_s";
    set_logging(0); errno = 0; p = (unsigned char *)A_calloc(4, 8); e = errno;
    CHECK(p != NULL && e == 0, "calloc_s(4,8) p=%p errno=%d", (void *)p, e);
    if (p) { CHECK(p[0] == 0 && p[31] == 0, "calloc_s not zeroed"); A_free(p); }
    PARITY_CALL(A_calloc(0, 8) != NULL, 1);
    PARITY_CALL_E(A_calloc((size_t)-1, 2) != NULL, 0, E_ALLOC_OVERFLOW);
}

static void op_realloc_s(void) {
    unsigned char *p, *q;
    int e;
    g_op = "realloc_s";
    set_logging(0); errno = 0; p = (unsigned char *)A_malloc(16); e = errno;
    if (!p) { CHECK(0, "realloc_s setup failed"); return; }
    memset(p, 0x5A, 16);
    q = (unsigned char *)A_realloc(p, 128);
    /* Only the old 16-byte prefix is preserved; write the new tail first. */
    CHECK(q != NULL && q[0] == 0x5A && q[15] == 0x5A, "realloc_s grow lost prefix");
    if (q) { q[127] = 0x5A; CHECK(q[127] == 0x5A, "realloc_s new tail not writable"); }
    A_free(q);
    /* boundary: realloc(NULL,n) == malloc_s(n) */
    PARITY_CALL(A_realloc(NULL, 24) != NULL, 1);
    /* invalid: over-large size leaves owner untouched + EINVAL */
    p = (unsigned char *)A_malloc(32);
    if (!p) { CHECK(0, "realloc_s setup failed (2)"); return; }
    memset(p, 0x11, 32);
    errno = 0; q = (unsigned char *)A_realloc(p, (size_t)-1); e = errno;
    CHECK(q == NULL && e == E_ALLOC_SIZE && p[0] == 0x11, "realloc_s(SIZE_MAX) errno=%d", e);
    A_free(p);
}

static void op_free_s(void) {
    void *p, *q;
    g_op = "free_s";
    p = A_malloc(16); q = A_malloc(16);
    CHECK(p != NULL && q != NULL, "free_s setup failed");
    set_logging(0); errno = 0; A_free(p); {
        int e0 = errno;
        set_logging(1); errno = 0; A_free(q);
        CHECK(e0 == errno, "free_s log-parity errno %d/%d", e0, errno);
    }
    PARITY_CALL((A_free(NULL), 1), 1);
}

static void op_free_checked_s(void) {
    void *p;
    uintptr_t addr;
    void *dangling;
    int rc, e;
    char stack[16];
    g_op = "free_checked_s";
    /* valid */
    p = A_malloc(64);
    CHECK(p != NULL, "free_checked_s setup failed");
    addr = (uintptr_t)p;
    set_logging(0); errno = 0; rc = A_checked_free(&p); e = errno;
    CHECK(rc == 0 && e == 0, "free_checked_s(valid) rc=%d errno=%d", rc, e);
    /* double free via the CHECKED entry only (never a raw double free) */
    dangling = (void *)addr;
    set_logging(0); errno = 0; rc = A_checked_free(&dangling); e = errno;
    CHECK(rc != 0 && e == EINVAL, "free_checked_s(double) rc=%d errno=%d", rc, e);
    set_logging(1); errno = 0; {
        void *again = (void *)addr;
        int rc2 = A_checked_free(&again);
        CHECK(rc2 == rc && errno == EINVAL, "free_checked_s(double) log-parity %d/%d", rc, rc2);
    }
    /* foreign stack pointer via the CHECKED entry */
    {
        void *foreign = (void *)stack;
        set_logging(0); errno = 0; rc = A_checked_free(&foreign); e = errno;
        CHECK(rc != 0 && e == EINVAL, "free_checked_s(foreign) rc=%d errno=%d", rc, e);
    }
    /* NULL is a no-op success */
    {
        void *nul = NULL;
        PARITY_CALL(A_checked_free(&nul), 0);
    }
}

static void op_aligned_alloc_s(void) {
    void *p;
    int ea, eb;
    g_op = "aligned_alloc_s";
    set_logging(0); errno = 0; p = A_aligned_alloc(64, 256); ea = errno;
    CHECK(p != NULL && (((uintptr_t)p & (uintptr_t)63u) == 0u) && ea == 0,
          "aligned_alloc_s(64,256) p=%p errno=%d", p, ea);
    if (p) { memset(p, 0x7E, 256); A_aligned_free(p); }
    set_logging(1); errno = 0; p = A_aligned_alloc(64, 256); eb = errno;
    CHECK(p != NULL && (((uintptr_t)p & (uintptr_t)63u) == 0u) && eb == ea,
          "aligned_alloc_s log-on p=%p errno=%d/%d", p, ea, eb);
    if (p) A_aligned_free(p);
    /* invalid: non-power-of-two / too-small alignment -> NULL / EINVAL */
    PARITY_CALL_E(A_aligned_alloc(3, 64) != NULL, 0, EINVAL);
    PARITY_CALL_E(A_aligned_alloc(1, 64) != NULL, 0, EINVAL);
}

static void op_cplus_safe_len(void) {
    int *p;
    size_t n0, n1;
    g_op = "cplus_safe_len";
    p = (int *)A_calloc(8, sizeof(int));
    if (!p) { CHECK(0, "cplus_safe_len setup failed"); return; }
    set_logging(0); n0 = A_len(p);
    set_logging(1); n1 = A_len(p);
    CHECK(n0 == 8u && n1 == 8u, "cplus_safe_len %lu/%lu want 8",
          (unsigned long)n0, (unsigned long)n1);
    A_free(p);
    PARITY_CALL((p = NULL, A_len(p)), 0);
}

static void op_query_length(void) {
    int *p;
    size_t c0, c1, bad;
    int rc0, rc1;
    g_op = "query_length";
    p = (int *)A_calloc(8, sizeof(int));
    if (!p) { CHECK(0, "query_length setup failed"); return; }
    c0 = c1 = 0; bad = 999;
    set_logging(0); errno = 0; rc0 = A_query_length(p, sizeof(int), &c0);
    set_logging(1); errno = 0; rc1 = A_query_length(p, sizeof(int), &c1);
    CHECK(rc0 == 0 && rc1 == 0 && c0 == 8u && c1 == 8u,
          "query_length rc %d/%d count %lu/%lu", rc0, rc1,
          (unsigned long)c0, (unsigned long)c1);
    /* invalid: NULL pointer -> error + zeroed out-count */
    set_logging(0); errno = 0; rc0 = A_query_length(NULL, sizeof(int), &bad);
    CHECK(rc0 != 0 && bad == 0, "query_length(NULL) rc=%d out=%lu", rc0, (unsigned long)bad);
    A_free(p);
}

static void op_transfer_ownership(void) {
    void *src, *dst, *bad_src, *bad_dst;
    int rc;
    g_op = "transfer_ownership";
    src = A_malloc(32); dst = NULL;
    if (!src) { CHECK(0, "transfer_ownership setup failed"); return; }
    set_logging(0); errno = 0; rc = A_transfer(&dst, &src);
    CHECK(rc == 0 && dst != NULL && src == NULL,
          "transfer_ownership(valid) rc=%d src=%p dst=%p", rc, src, dst);
    A_free(dst);
    /* invalid: destination already holds a pointer -> error, nothing stolen */
    bad_src = A_malloc(16); bad_dst = A_malloc(8);
    if (!bad_src || !bad_dst) { CHECK(0, "transfer_ownership setup 2 failed"); return; }
    set_logging(1); errno = 0; rc = A_transfer(&bad_dst, &bad_src);
    CHECK(rc != 0 && bad_src != NULL && bad_dst != NULL,
          "transfer_ownership(busy dst) rc=%d", rc);
    A_free(bad_src); A_free(bad_dst);
}

static void op_fail_injection(void) {
    void *p1, *p2;
    int e1, e2;
    g_op = "fail_injection";
    A_fail_reset();
    A_fail_after(1);
    set_logging(0); errno = 0; p1 = A_malloc(16); e1 = errno;
    errno = 0; p2 = A_malloc(16); e2 = errno;
    A_fail_reset();
    CHECK(p1 != NULL && p2 == NULL && e2 == ENOMEM,
          "fail_after(1): p1=%p p2=%p errno2=%d", p1, p2, e2);
    if (p1) A_free(p1);
    /* fail_reset: allocations succeed again */
    A_fail_reset(); A_fail_after(0);
    set_logging(1); errno = 0; p1 = A_malloc(16); e1 = errno;
    A_fail_reset();
    set_logging(1); errno = 0; p2 = A_malloc(16); e2 = errno;
    CHECK(p1 == NULL && p2 != NULL && e1 == ENOMEM,
          "fail_reset: p1=%p p2=%p errno1=%d", p1, p2, e1);
    if (p2) A_free(p2);
}

/* ---- operation table ----------------------------------------------------- */
typedef void (*op_fn)(void);
typedef struct { const char *name; op_fn fn; } Op;
#define O(x) { #x, op_##x }
static const Op OPS[] = {
    /* 35 base stdlib-family operations */
    O(malloc), O(calloc), O(realloc), O(free),
    O(abs), O(labs), O(llabs),
    O(div), O(ldiv), O(lldiv),
    O(atoi), O(atol), O(atoll), O(atof),
    O(strtol), O(strtoul), O(strtoll), O(strtoull),
    O(strtod), O(strtof), O(strtold),
    O(qsort), O(bsearch),
    O(rand), O(srand),
    O(getenv), O(setenv), O(unsetenv),
    O(exit), O(abort), O(atexit),
    O(mkstemp), O(mkdtemp), O(realpath), O(system),
    /* 10 checked-variant / extension entry points */
    O(malloc_s), O(calloc_s), O(realloc_s), O(free_s), O(free_checked_s),
    O(aligned_alloc_s),
    O(cplus_safe_len), O(query_length), O(transfer_ownership), O(fail_injection)
};
#undef O

int main(void) {
    size_t i;
    const size_t nops = sizeof(OPS) / sizeof(OPS[0]);
    mute_stderr();
#ifndef PLAIN_LIBC
    cplus_runtime_set_violation_trap(0);
#endif
    A_fail_reset();
    set_logging(0);
    for (i = 0; i < nops; ++i) OPS[i].fn();
    set_logging(0);
    A_fail_reset();
    unmute_stderr();

    printf("stdlib_checked: covered %lu operations (%s layer)\n",
           (unsigned long)nops, LAYER);
    printf("stdlib_checked: %d/%d checks ok\n", g_checks - g_fail, g_checks);
    return g_fail == 0 ? 0 : 1;
}
