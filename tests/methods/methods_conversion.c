/* methods_conversion.c -- per-method unit + logging tests for the numeric
 * conversion and ctype methods of the C+ hardened libc.
 *
 * Build (from repository root):
 *   /usr/bin/clang -O1 -g -std=c99 -Wall -Wextra -pedantic -I. -Ilibc \
 *       -Ilibc/install-include tests/methods/methods_conversion.c \
 *       build/libc/libcplus_hardened.a -lm -pthread
 *
 * Every scenario runs the cplus_* wrapper twice -- once with the opt-in libc
 * logger OFF and once with it ON -- and compares return value, errno and
 * endptr offset.  Well-defined inputs are additionally compared against the
 * exact installed host libc backend.  NULL is only passed to wrappers that
 * document a NULL guard; EOF/WEOF are exercised for every ctype entry point.
 * No signed overflow, division by zero or out-of-domain ctype argument is
 * used, so the test itself contains no genuine UB.
 */
#include "cplus_libc.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

static int g_checks = 0;
static int g_fail = 0;
static int g_saved_stderr = -1;

#define CHECK(cond, ...)                                     \
    do {                                                     \
        ++g_checks;                                          \
        if (!(cond)) {                                       \
            ++g_fail;                                        \
            printf("FINDING: %s:%d: ", __func__, __LINE__);  \
            printf(__VA_ARGS__);                             \
            putchar('\n');                                   \
        }                                                    \
    } while (0)

/* The logger writes to fd 2 with write(2); silence it so a full run produces
 * only this test's stdout.  stdout (where FINDINGs go) is untouched. */
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

static void log_off(void) { cplus_libc_log_set_enabled(0); }
static void log_on(void) { cplus_libc_log_set_enabled(1); }

static int same_float(float a, float b) {
    return memcmp(&a, &b, sizeof a) == 0;
}
static int same_double(double a, double b) {
    return memcmp(&a, &b, sizeof a) == 0;
}
static int same_ldouble(long double a, long double b) {
    /* long double may contain padding, so do not memcmp it. */
    return a == b && signbit(a) == signbit(b);
}

/* ------------------------------------------------------------------ */
/* Generic runners for integer strto* families.                        */
/* want_errno < 0 / want_off < 0 disable that particular assertion.    */
/* ------------------------------------------------------------------ */
typedef long (*fn_long)(const char *, char **, int);
typedef unsigned long (*fn_ulong)(const char *, char **, int);
typedef long long (*fn_llong)(const char *, char **, int);
typedef unsigned long long (*fn_ullong)(const char *, char **, int);
typedef intmax_t (*fn_imax)(const char *, char **, int);
typedef uintmax_t (*fn_umax)(const char *, char **, int);

static void run_long(fn_long cf, fn_long hf, const char *nm, const char *s,
                     int base, int give_end, long want, int want_errno,
                     ptrdiff_t want_off) {
    char *e0 = NULL, *e1 = NULL, *eh = NULL;
    long r0, r1, rh;
    int n0, n1, nh;
    log_off(); errno = 0; r0 = cf(s, give_end ? &e0 : NULL, base); n0 = errno;
    log_on();  errno = 0; r1 = cf(s, give_end ? &e1 : NULL, base); n1 = errno;
    log_off(); errno = 0; rh = hf(s, give_end ? &eh : NULL, base); nh = errno;
    CHECK(r0 == r1, "%s('%s',%d) log parity ret %ld/%ld", nm, s, base, r0, r1);
    CHECK(n0 == n1, "%s('%s',%d) log parity errno %d/%d", nm, s, base, n0, n1);
    if (give_end)
        CHECK((const char *)e0 - s == (const char *)e1 - s,
              "%s('%s',%d) log parity endptr %td/%td", nm, s, base,
              (const char *)e0 - s, (const char *)e1 - s);
    CHECK(r0 == rh, "%s('%s',%d) host diff ret %ld/%ld", nm, s, base, r0, rh);
    CHECK(n0 == nh, "%s('%s',%d) host diff errno %d/%d", nm, s, base, n0, nh);
    if (give_end)
        CHECK((const char *)e0 - s == (const char *)eh - s,
              "%s('%s',%d) host diff endptr %td/%td", nm, s, base,
              (const char *)e0 - s, (const char *)eh - s);
    CHECK(r0 == want, "%s('%s',%d) expected ret %ld got %ld", nm, s, base, want, r0);
    if (want_errno >= 0)
        CHECK(n0 == want_errno, "%s('%s',%d) expected errno %d got %d",
              nm, s, base, want_errno, n0);
    if (give_end && want_off >= 0)
        CHECK((const char *)e0 - s == want_off,
              "%s('%s',%d) expected endptr %td got %td", nm, s, base,
              want_off, (const char *)e0 - s);
}

static void run_ulong(fn_ulong cf, fn_ulong hf, const char *nm, const char *s,
                      int base, int give_end, unsigned long want,
                      int want_errno, ptrdiff_t want_off) {
    char *e0 = NULL, *e1 = NULL, *eh = NULL;
    unsigned long r0, r1, rh;
    int n0, n1, nh;
    log_off(); errno = 0; r0 = cf(s, give_end ? &e0 : NULL, base); n0 = errno;
    log_on();  errno = 0; r1 = cf(s, give_end ? &e1 : NULL, base); n1 = errno;
    log_off(); errno = 0; rh = hf(s, give_end ? &eh : NULL, base); nh = errno;
    CHECK(r0 == r1, "%s('%s',%d) log parity ret %lu/%lu", nm, s, base, r0, r1);
    CHECK(n0 == n1, "%s('%s',%d) log parity errno %d/%d", nm, s, base, n0, n1);
    if (give_end)
        CHECK((const char *)e0 - s == (const char *)e1 - s,
              "%s('%s',%d) log parity endptr %td/%td", nm, s, base,
              (const char *)e0 - s, (const char *)e1 - s);
    CHECK(r0 == rh, "%s('%s',%d) host diff ret %lu/%lu", nm, s, base, r0, rh);
    CHECK(n0 == nh, "%s('%s',%d) host diff errno %d/%d", nm, s, base, n0, nh);
    if (give_end)
        CHECK((const char *)e0 - s == (const char *)eh - s,
              "%s('%s',%d) host diff endptr %td/%td", nm, s, base,
              (const char *)e0 - s, (const char *)eh - s);
    CHECK(r0 == want, "%s('%s',%d) expected ret %lu got %lu", nm, s, base, want, r0);
    if (want_errno >= 0)
        CHECK(n0 == want_errno, "%s('%s',%d) expected errno %d got %d",
              nm, s, base, want_errno, n0);
    if (give_end && want_off >= 0)
        CHECK((const char *)e0 - s == want_off,
              "%s('%s',%d) expected endptr %td got %td", nm, s, base,
              want_off, (const char *)e0 - s);
}

static void run_llong(fn_llong cf, fn_llong hf, const char *nm, const char *s,
                      int base, int give_end, long long want, int want_errno,
                      ptrdiff_t want_off) {
    char *e0 = NULL, *e1 = NULL, *eh = NULL;
    long long r0, r1, rh;
    int n0, n1, nh;
    log_off(); errno = 0; r0 = cf(s, give_end ? &e0 : NULL, base); n0 = errno;
    log_on();  errno = 0; r1 = cf(s, give_end ? &e1 : NULL, base); n1 = errno;
    log_off(); errno = 0; rh = hf(s, give_end ? &eh : NULL, base); nh = errno;
    CHECK(r0 == r1, "%s('%s',%d) log parity ret %lld/%lld", nm, s, base, r0, r1);
    CHECK(n0 == n1, "%s('%s',%d) log parity errno %d/%d", nm, s, base, n0, n1);
    if (give_end)
        CHECK((const char *)e0 - s == (const char *)e1 - s,
              "%s('%s',%d) log parity endptr %td/%td", nm, s, base,
              (const char *)e0 - s, (const char *)e1 - s);
    CHECK(r0 == rh, "%s('%s',%d) host diff ret %lld/%lld", nm, s, base, r0, rh);
    CHECK(n0 == nh, "%s('%s',%d) host diff errno %d/%d", nm, s, base, n0, nh);
    if (give_end)
        CHECK((const char *)e0 - s == (const char *)eh - s,
              "%s('%s',%d) host diff endptr %td/%td", nm, s, base,
              (const char *)e0 - s, (const char *)eh - s);
    CHECK(r0 == want, "%s('%s',%d) expected ret %lld got %lld", nm, s, base, want, r0);
    if (want_errno >= 0)
        CHECK(n0 == want_errno, "%s('%s',%d) expected errno %d got %d",
              nm, s, base, want_errno, n0);
    if (give_end && want_off >= 0)
        CHECK((const char *)e0 - s == want_off,
              "%s('%s',%d) expected endptr %td got %td", nm, s, base,
              want_off, (const char *)e0 - s);
}

static void run_ullong(fn_ullong cf, fn_ullong hf, const char *nm, const char *s,
                       int base, int give_end, unsigned long long want,
                       int want_errno, ptrdiff_t want_off) {
    char *e0 = NULL, *e1 = NULL, *eh = NULL;
    unsigned long long r0, r1, rh;
    int n0, n1, nh;
    log_off(); errno = 0; r0 = cf(s, give_end ? &e0 : NULL, base); n0 = errno;
    log_on();  errno = 0; r1 = cf(s, give_end ? &e1 : NULL, base); n1 = errno;
    log_off(); errno = 0; rh = hf(s, give_end ? &eh : NULL, base); nh = errno;
    CHECK(r0 == r1, "%s('%s',%d) log parity ret %llu/%llu", nm, s, base, r0, r1);
    CHECK(n0 == n1, "%s('%s',%d) log parity errno %d/%d", nm, s, base, n0, n1);
    if (give_end)
        CHECK((const char *)e0 - s == (const char *)e1 - s,
              "%s('%s',%d) log parity endptr %td/%td", nm, s, base,
              (const char *)e0 - s, (const char *)e1 - s);
    CHECK(r0 == rh, "%s('%s',%d) host diff ret %llu/%llu", nm, s, base, r0, rh);
    CHECK(n0 == nh, "%s('%s',%d) host diff errno %d/%d", nm, s, base, n0, nh);
    if (give_end)
        CHECK((const char *)e0 - s == (const char *)eh - s,
              "%s('%s',%d) host diff endptr %td/%td", nm, s, base,
              (const char *)e0 - s, (const char *)eh - s);
    CHECK(r0 == want, "%s('%s',%d) expected ret %llu got %llu", nm, s, base, want, r0);
    if (want_errno >= 0)
        CHECK(n0 == want_errno, "%s('%s',%d) expected errno %d got %d",
              nm, s, base, want_errno, n0);
    if (give_end && want_off >= 0)
        CHECK((const char *)e0 - s == want_off,
              "%s('%s',%d) expected endptr %td got %td", nm, s, base,
              want_off, (const char *)e0 - s);
}

static void run_imax(fn_imax cf, fn_imax hf, const char *nm, const char *s,
                     int base, int give_end, intmax_t want, int want_errno,
                     ptrdiff_t want_off) {
    char *e0 = NULL, *e1 = NULL, *eh = NULL;
    intmax_t r0, r1, rh;
    int n0, n1, nh;
    log_off(); errno = 0; r0 = cf(s, give_end ? &e0 : NULL, base); n0 = errno;
    log_on();  errno = 0; r1 = cf(s, give_end ? &e1 : NULL, base); n1 = errno;
    log_off(); errno = 0; rh = hf(s, give_end ? &eh : NULL, base); nh = errno;
    CHECK(r0 == r1, "%s('%s',%d) log parity ret %jd/%jd", nm, s, base, r0, r1);
    CHECK(n0 == n1, "%s('%s',%d) log parity errno %d/%d", nm, s, base, n0, n1);
    if (give_end)
        CHECK((const char *)e0 - s == (const char *)e1 - s,
              "%s('%s',%d) log parity endptr %td/%td", nm, s, base,
              (const char *)e0 - s, (const char *)e1 - s);
    CHECK(r0 == rh, "%s('%s',%d) host diff ret %jd/%jd", nm, s, base, r0, rh);
    CHECK(n0 == nh, "%s('%s',%d) host diff errno %d/%d", nm, s, base, n0, nh);
    if (give_end)
        CHECK((const char *)e0 - s == (const char *)eh - s,
              "%s('%s',%d) host diff endptr %td/%td", nm, s, base,
              (const char *)e0 - s, (const char *)eh - s);
    CHECK(r0 == want, "%s('%s',%d) expected ret %jd got %jd", nm, s, base, want, r0);
    if (want_errno >= 0)
        CHECK(n0 == want_errno, "%s('%s',%d) expected errno %d got %d",
              nm, s, base, want_errno, n0);
    if (give_end && want_off >= 0)
        CHECK((const char *)e0 - s == want_off,
              "%s('%s',%d) expected endptr %td got %td", nm, s, base,
              want_off, (const char *)e0 - s);
}

static void run_umax(fn_umax cf, fn_umax hf, const char *nm, const char *s,
                     int base, int give_end, uintmax_t want, int want_errno,
                     ptrdiff_t want_off) {
    char *e0 = NULL, *e1 = NULL, *eh = NULL;
    uintmax_t r0, r1, rh;
    int n0, n1, nh;
    log_off(); errno = 0; r0 = cf(s, give_end ? &e0 : NULL, base); n0 = errno;
    log_on();  errno = 0; r1 = cf(s, give_end ? &e1 : NULL, base); n1 = errno;
    log_off(); errno = 0; rh = hf(s, give_end ? &eh : NULL, base); nh = errno;
    CHECK(r0 == r1, "%s('%s',%d) log parity ret %ju/%ju", nm, s, base, r0, r1);
    CHECK(n0 == n1, "%s('%s',%d) log parity errno %d/%d", nm, s, base, n0, n1);
    if (give_end)
        CHECK((const char *)e0 - s == (const char *)e1 - s,
              "%s('%s',%d) log parity endptr %td/%td", nm, s, base,
              (const char *)e0 - s, (const char *)e1 - s);
    CHECK(r0 == rh, "%s('%s',%d) host diff ret %ju/%ju", nm, s, base, r0, rh);
    CHECK(n0 == nh, "%s('%s',%d) host diff errno %d/%d", nm, s, base, n0, nh);
    if (give_end)
        CHECK((const char *)e0 - s == (const char *)eh - s,
              "%s('%s',%d) host diff endptr %td/%td", nm, s, base,
              (const char *)e0 - s, (const char *)eh - s);
    CHECK(r0 == want, "%s('%s',%d) expected ret %ju got %ju", nm, s, base, want, r0);
    if (want_errno >= 0)
        CHECK(n0 == want_errno, "%s('%s',%d) expected errno %d got %d",
              nm, s, base, want_errno, n0);
    if (give_end && want_off >= 0)
        CHECK((const char *)e0 - s == want_off,
              "%s('%s',%d) expected endptr %td got %td", nm, s, base,
              want_off, (const char *)e0 - s);
}

/* ------------------------------------------------------------------ */
/* Runners for floating-point strto* families.                         */
/* ------------------------------------------------------------------ */
typedef float (*fn_float)(const char *, char **);
typedef double (*fn_double)(const char *, char **);
typedef long double (*fn_ldouble)(const char *, char **);

static void run_float(fn_float cf, fn_float hf, const char *nm, const char *s,
                      float want, int want_errno, ptrdiff_t want_off) {
    char *e0 = NULL, *e1 = NULL, *eh = NULL;
    float r0, r1, rh;
    int n0, n1, nh;
    log_off(); errno = 0; r0 = cf(s, &e0); n0 = errno;
    log_on();  errno = 0; r1 = cf(s, &e1); n1 = errno;
    log_off(); errno = 0; rh = hf(s, &eh); nh = errno;
    CHECK(same_float(r0, r1), "%s('%s') log parity ret %g/%g", nm, s, (double)r0, (double)r1);
    CHECK(n0 == n1, "%s('%s') log parity errno %d/%d", nm, s, n0, n1);
    CHECK((const char *)e0 - s == (const char *)e1 - s,
          "%s('%s') log parity endptr %td/%td", nm, s,
          (const char *)e0 - s, (const char *)e1 - s);
    CHECK(same_float(r0, rh), "%s('%s') host diff ret %g/%g", nm, s, (double)r0, (double)rh);
    CHECK(n0 == nh, "%s('%s') host diff errno %d/%d", nm, s, n0, nh);
    CHECK((const char *)e0 - s == (const char *)eh - s,
          "%s('%s') host diff endptr %td/%td", nm, s,
          (const char *)e0 - s, (const char *)eh - s);
    CHECK(same_float(r0, want), "%s('%s') expected ret %g got %g", nm, s, (double)want, (double)r0);
    if (want_errno >= 0)
        CHECK(n0 == want_errno, "%s('%s') expected errno %d got %d", nm, s, want_errno, n0);
    if (want_off >= 0)
        CHECK((const char *)e0 - s == want_off, "%s('%s') expected endptr %td got %td",
              nm, s, want_off, (const char *)e0 - s);
}

static void run_double(fn_double cf, fn_double hf, const char *nm, const char *s,
                       double want, int want_errno, ptrdiff_t want_off) {
    char *e0 = NULL, *e1 = NULL, *eh = NULL;
    double r0, r1, rh;
    int n0, n1, nh;
    log_off(); errno = 0; r0 = cf(s, &e0); n0 = errno;
    log_on();  errno = 0; r1 = cf(s, &e1); n1 = errno;
    log_off(); errno = 0; rh = hf(s, &eh); nh = errno;
    CHECK(same_double(r0, r1), "%s('%s') log parity ret %g/%g", nm, s, r0, r1);
    CHECK(n0 == n1, "%s('%s') log parity errno %d/%d", nm, s, n0, n1);
    CHECK((const char *)e0 - s == (const char *)e1 - s,
          "%s('%s') log parity endptr %td/%td", nm, s,
          (const char *)e0 - s, (const char *)e1 - s);
    CHECK(same_double(r0, rh), "%s('%s') host diff ret %g/%g", nm, s, r0, rh);
    CHECK(n0 == nh, "%s('%s') host diff errno %d/%d", nm, s, n0, nh);
    CHECK((const char *)e0 - s == (const char *)eh - s,
          "%s('%s') host diff endptr %td/%td", nm, s,
          (const char *)e0 - s, (const char *)eh - s);
    CHECK(same_double(r0, want), "%s('%s') expected ret %g got %g", nm, s, want, r0);
    if (want_errno >= 0)
        CHECK(n0 == want_errno, "%s('%s') expected errno %d got %d", nm, s, want_errno, n0);
    if (want_off >= 0)
        CHECK((const char *)e0 - s == want_off, "%s('%s') expected endptr %td got %td",
              nm, s, want_off, (const char *)e0 - s);
}

static void run_ldouble(fn_ldouble cf, fn_ldouble hf, const char *nm, const char *s,
                        long double want, int want_errno, ptrdiff_t want_off) {
    char *e0 = NULL, *e1 = NULL, *eh = NULL;
    long double r0, r1, rh;
    int n0, n1, nh;
    log_off(); errno = 0; r0 = cf(s, &e0); n0 = errno;
    log_on();  errno = 0; r1 = cf(s, &e1); n1 = errno;
    log_off(); errno = 0; rh = hf(s, &eh); nh = errno;
    CHECK(same_ldouble(r0, r1), "%s('%s') log parity ret %Lg/%Lg", nm, s, r0, r1);
    CHECK(n0 == n1, "%s('%s') log parity errno %d/%d", nm, s, n0, n1);
    CHECK((const char *)e0 - s == (const char *)e1 - s,
          "%s('%s') log parity endptr %td/%td", nm, s,
          (const char *)e0 - s, (const char *)e1 - s);
    CHECK(same_ldouble(r0, rh), "%s('%s') host diff ret %Lg/%Lg", nm, s, r0, rh);
    CHECK(n0 == nh, "%s('%s') host diff errno %d/%d", nm, s, n0, nh);
    CHECK((const char *)e0 - s == (const char *)eh - s,
          "%s('%s') host diff endptr %td/%td", nm, s,
          (const char *)e0 - s, (const char *)eh - s);
    CHECK(same_ldouble(r0, want), "%s('%s') expected ret %Lg got %Lg", nm, s, want, r0);
    if (want_errno >= 0)
        CHECK(n0 == want_errno, "%s('%s') expected errno %d got %d", nm, s, want_errno, n0);
    if (want_off >= 0)
        CHECK((const char *)e0 - s == want_off, "%s('%s') expected endptr %td got %td",
              nm, s, want_off, (const char *)e0 - s);
}

/* ------------------------------------------------------------------ */
/* Runners for NULL-guarded no-endptr families.                        */
/* ------------------------------------------------------------------ */
static void run_atoi(const char *nm, const char *s, int want, int want_errno) {
    int r0, r1, rh; int n0, n1, nh;
    log_off(); errno = 0; r0 = cplus_atoi(s); n0 = errno;
    log_on();  errno = 0; r1 = cplus_atoi(s); n1 = errno;
    log_off(); errno = 0; rh = (s ? atoi(s) : 0); nh = errno;
    CHECK(r0 == r1, "%s log parity ret %d/%d", nm, r0, r1);
    CHECK(n0 == n1, "%s log parity errno %d/%d", nm, n0, n1);
    if (s) {
        CHECK(r0 == rh, "%s host diff ret %d/%d", nm, r0, rh);
        CHECK(n0 == nh, "%s host diff errno %d/%d", nm, n0, nh);
    }
    CHECK(r0 == want, "%s expected ret %d got %d", nm, want, r0);
    if (want_errno >= 0)
        CHECK(n0 == want_errno, "%s expected errno %d got %d", nm, want_errno, n0);
}

static void run_atol(const char *nm, const char *s, long want, int want_errno) {
    long r0, r1, rh; int n0, n1, nh;
    log_off(); errno = 0; r0 = cplus_atol(s); n0 = errno;
    log_on();  errno = 0; r1 = cplus_atol(s); n1 = errno;
    log_off(); errno = 0; rh = (s ? atol(s) : 0); nh = errno;
    CHECK(r0 == r1, "%s log parity ret %ld/%ld", nm, r0, r1);
    CHECK(n0 == n1, "%s log parity errno %d/%d", nm, n0, n1);
    if (s) {
        CHECK(r0 == rh, "%s host diff ret %ld/%ld", nm, r0, rh);
        CHECK(n0 == nh, "%s host diff errno %d/%d", nm, n0, nh);
    }
    CHECK(r0 == want, "%s expected ret %ld got %ld", nm, want, r0);
    if (want_errno >= 0)
        CHECK(n0 == want_errno, "%s expected errno %d got %d", nm, want_errno, n0);
}

/* do_diff is false for inputs with no digits: the musl-derived wrapper does
 * not manufacture EINVAL where the host strtoll backend does, and the task's
 * host differential is deliberately scoped to valid inputs. */
static void run_atoll(const char *nm, const char *s, long long want,
                      int want_errno, int do_diff) {
    long long r0, r1, rh; int n0, n1, nh;
    log_off(); errno = 0; r0 = cplus_atoll(s); n0 = errno;
    log_on();  errno = 0; r1 = cplus_atoll(s); n1 = errno;
    log_off(); errno = 0; rh = (s && do_diff ? atoll(s) : 0); nh = errno;
    CHECK(r0 == r1, "%s log parity ret %lld/%lld", nm, r0, r1);
    CHECK(n0 == n1, "%s log parity errno %d/%d", nm, n0, n1);
    if (s && do_diff) {
        CHECK(r0 == rh, "%s host diff ret %lld/%lld", nm, r0, rh);
        CHECK(n0 == nh, "%s host diff errno %d/%d", nm, n0, nh);
    }
    CHECK(r0 == want, "%s expected ret %lld got %lld", nm, want, r0);
    if (want_errno >= 0)
        CHECK(n0 == want_errno, "%s expected errno %d got %d", nm, want_errno, n0);
}

static void run_atof(const char *nm, const char *s, double want, int want_errno) {
    double r0, r1, rh; int n0, n1, nh;
    log_off(); errno = 0; r0 = cplus_atof(s); n0 = errno;
    log_on();  errno = 0; r1 = cplus_atof(s); n1 = errno;
    log_off(); errno = 0; rh = (s ? atof(s) : 0.0); nh = errno;
    CHECK(same_double(r0, r1), "%s log parity ret %g/%g", nm, r0, r1);
    CHECK(n0 == n1, "%s log parity errno %d/%d", nm, n0, n1);
    if (s) {
        CHECK(same_double(r0, rh), "%s host diff ret %g/%g", nm, r0, rh);
        CHECK(n0 == nh, "%s host diff errno %d/%d", nm, n0, nh);
    }
    CHECK(same_double(r0, want), "%s expected ret %g got %g", nm, want, r0);
    if (want_errno >= 0)
        CHECK(n0 == want_errno, "%s expected errno %d got %d", nm, want_errno, n0);
}

/* ------------------------------------------------------------------ */
/* 01 strtol                                                          */
/* ------------------------------------------------------------------ */
static void m_conv_01_strtol(void) {
    /* METHOD: strtol -- decimal/hex/octal, whitespace/sign, invalid+endptr,
     * ERANGE overflow, bases 1/37/INT_MIN, NULL endptr, logging parity. */
    run_long(cplus_strtol, strtol, "strtol", "123", 10, 1, 123, 0, 3);
    run_long(cplus_strtol, strtol, "strtol", "  -42", 10, 1, -42, 0, 5);
    run_long(cplus_strtol, strtol, "strtol", "\t+77xyz", 10, 1, 77, 0, 4);
    run_long(cplus_strtol, strtol, "strtol", "0x2A", 0, 1, 42, 0, 4);
    run_long(cplus_strtol, strtol, "strtol", "052", 0, 1, 42, 0, 3);
    run_long(cplus_strtol, strtol, "strtol", "0x2A", 16, 1, 42, 0, 4);
    run_long(cplus_strtol, strtol, "strtol", "abc", 10, 1, 0, EINVAL, 0);
    run_long(cplus_strtol, strtol, "strtol", "123abc", 10, 1, 123, 0, 3);
    run_long(cplus_strtol, strtol, "strtol", "", 10, 1, 0, EINVAL, 0);
    run_long(cplus_strtol, strtol, "strtol", "9999999999999999999999999999999999", 10, 1,
             LONG_MAX, ERANGE, 34);
    run_long(cplus_strtol, strtol, "strtol", "1", 1, 1, 0, EINVAL, 0);
    run_long(cplus_strtol, strtol, "strtol", "1", 37, 1, 0, EINVAL, 0);
    run_long(cplus_strtol, strtol, "strtol", "1", INT_MIN, 1, 0, EINVAL, 0);
    run_long(cplus_strtol, strtol, "strtol", "-42", 10, 0, -42, 0, -1);
}

/* ------------------------------------------------------------------ */
/* 02 strtoul                                                         */
/* ------------------------------------------------------------------ */
static void m_conv_02_strtoul(void) {
    /* METHOD: strtoul -- decimal/hex/octal, sign wrap, invalid+endptr,
     * ERANGE overflow, bad bases, NULL endptr, logging parity. */
    run_ulong(cplus_strtoul, strtoul, "strtoul", "42", 10, 1, 42UL, 0, 2);
    run_ulong(cplus_strtoul, strtoul, "strtoul", "-1", 10, 1, ULONG_MAX, -1, 2);
    run_ulong(cplus_strtoul, strtoul, "strtoul", "0xFF", 0, 1, 255UL, 0, 4);
    run_ulong(cplus_strtoul, strtoul, "strtoul", "077", 0, 1, 63UL, 0, 3);
    run_ulong(cplus_strtoul, strtoul, "strtoul", "xyz", 10, 1, 0UL, EINVAL, 0);
    run_ulong(cplus_strtoul, strtoul, "strtoul", "184467440737095516160", 10, 1,
              ULONG_MAX, ERANGE, 21);
    run_ulong(cplus_strtoul, strtoul, "strtoul", "1", 1, 1, 0UL, EINVAL, 0);
    run_ulong(cplus_strtoul, strtoul, "strtoul", "1", 37, 1, 0UL, EINVAL, 0);
    run_ulong(cplus_strtoul, strtoul, "strtoul", "1", INT_MIN, 1, 0UL, EINVAL, 0);
    run_ulong(cplus_strtoul, strtoul, "strtoul", "  +7!", 10, 0, 7UL, 0, -1);
}

/* ------------------------------------------------------------------ */
/* 03 strtoll                                                         */
/* ------------------------------------------------------------------ */
static void m_conv_03_strtoll(void) {
    /* METHOD: strtoll -- decimal/hex/octal, whitespace/sign, invalid+endptr,
     * ERANGE overflow, bad bases, NULL endptr, logging parity. */
    run_llong(cplus_strtoll, strtoll, "strtoll", "123", 10, 1, 123LL, 0, 3);
    run_llong(cplus_strtoll, strtoll, "strtoll", "  -42", 10, 1, -42LL, 0, 5);
    run_llong(cplus_strtoll, strtoll, "strtoll", "0x10", 0, 1, 16LL, 0, 4);
    run_llong(cplus_strtoll, strtoll, "strtoll", "010", 0, 1, 8LL, 0, 3);
    run_llong(cplus_strtoll, strtoll, "strtoll", "junk", 10, 1, 0LL, EINVAL, 0);
    run_llong(cplus_strtoll, strtoll, "strtoll", "9223372036854775808", 10, 1,
              LLONG_MAX, ERANGE, 19);
    run_llong(cplus_strtoll, strtoll, "strtoll", "-9223372036854775809", 10, 1,
              LLONG_MIN, ERANGE, 20);
    run_llong(cplus_strtoll, strtoll, "strtoll", "1", 1, 1, 0LL, EINVAL, 0);
    run_llong(cplus_strtoll, strtoll, "strtoll", "1", 37, 1, 0LL, EINVAL, 0);
    run_llong(cplus_strtoll, strtoll, "strtoll", "1", INT_MIN, 1, 0LL, EINVAL, 0);
    run_llong(cplus_strtoll, strtoll, "strtoll", "0x1f", 16, 0, 31LL, 0, -1);
}

/* ------------------------------------------------------------------ */
/* 04 strtoull                                                        */
/* ------------------------------------------------------------------ */
static void m_conv_04_strtoull(void) {
    /* METHOD: strtoull -- decimal/hex/octal, sign, invalid+endptr,
     * ERANGE overflow, bad bases, NULL endptr, logging parity. */
    run_ullong(cplus_strtoull, strtoull, "strtoull", "123", 10, 1, 123ULL, 0, 3);
    run_ullong(cplus_strtoull, strtoull, "strtoull", "  +42", 10, 1, 42ULL, 0, 5);
    run_ullong(cplus_strtoull, strtoull, "strtoull", "0xAb", 0, 1, 171ULL, 0, 4);
    run_ullong(cplus_strtoull, strtoull, "strtoull", "0777", 0, 1, 511ULL, 0, 4);
    run_ullong(cplus_strtoull, strtoull, "strtoull", "zzz", 10, 1, 0ULL, EINVAL, 0);
    run_ullong(cplus_strtoull, strtoull, "strtoull", "184467440737095516160", 10, 1,
               ULLONG_MAX, ERANGE, 21);
    run_ullong(cplus_strtoull, strtoull, "strtoull", "1", 1, 1, 0ULL, EINVAL, 0);
    run_ullong(cplus_strtoull, strtoull, "strtoull", "1", 37, 1, 0ULL, EINVAL, 0);
    run_ullong(cplus_strtoull, strtoull, "strtoull", "1", INT_MIN, 1, 0ULL, EINVAL, 0);
    run_ullong(cplus_strtoull, strtoull, "strtoull", "-7", 10, 0,
               (unsigned long long)(-7LL), 0, -1);
}

/* ------------------------------------------------------------------ */
/* 05 strtof                                                          */
/* ------------------------------------------------------------------ */
static void m_conv_05_strtof(void) {
    /* METHOD: strtof -- valid decimal/hex, whitespace/sign, invalid+endptr,
     * ERANGE overflow/underflow, logging parity, host differential. */
    run_float(cplus_strtof, strtof, "strtof", "1.5", 1.5f, 0, 3);
    run_float(cplus_strtof, strtof, "strtof", "  -2.5xyz", -2.5f, 0, 6);
    run_float(cplus_strtof, strtof, "strtof", "0x1.8p1", 3.0f, 0, 7);
    run_float(cplus_strtof, strtof, "strtof", "abc", 0.0f, 0, 0);
    run_float(cplus_strtof, strtof, "strtof", "1e9999", HUGE_VALF, ERANGE, 6);
    run_float(cplus_strtof, strtof, "strtof", "-1e9999", -HUGE_VALF, ERANGE, 7);
    run_float(cplus_strtof, strtof, "strtof", "1e-9999", 0.0f, ERANGE, 7);
}

/* ------------------------------------------------------------------ */
/* 06 strtod                                                          */
/* ------------------------------------------------------------------ */
static void m_conv_06_strtod(void) {
    /* METHOD: strtod -- valid decimal/hex, whitespace/sign, invalid+endptr,
     * ERANGE overflow/underflow, logging parity, host differential. */
    run_double(cplus_strtod, strtod, "strtod", "1.5", 1.5, 0, 3);
    run_double(cplus_strtod, strtod, "strtod", "  -2.5xyz", -2.5, 0, 6);
    run_double(cplus_strtod, strtod, "strtod", "0x1.8p1", 3.0, 0, 7);
    run_double(cplus_strtod, strtod, "strtod", "abc", 0.0, 0, 0);
    run_double(cplus_strtod, strtod, "strtod", "1e9999", HUGE_VAL, ERANGE, 6);
    run_double(cplus_strtod, strtod, "strtod", "-1e9999", -HUGE_VAL, ERANGE, 7);
    run_double(cplus_strtod, strtod, "strtod", "1e-9999", 0.0, ERANGE, 7);
}

/* ------------------------------------------------------------------ */
/* 07 strtold                                                         */
/* ------------------------------------------------------------------ */
static void m_conv_07_strtold(void) {
    /* METHOD: strtold -- valid decimal/hex, whitespace/sign, invalid+endptr,
     * ERANGE overflow/underflow, logging parity, host differential. */
    run_ldouble(cplus_strtold, strtold, "strtold", "1.5L", 1.5L, 0, 3);
    run_ldouble(cplus_strtold, strtold, "strtold", "  -2.5xyz", -2.5L, 0, 6);
    run_ldouble(cplus_strtold, strtold, "strtold", "0x1.8p1", 3.0L, 0, 7);
    run_ldouble(cplus_strtold, strtold, "strtold", "abc", 0.0L, 0, 0);
    run_ldouble(cplus_strtold, strtold, "strtold", "1e9999", HUGE_VALL, ERANGE, 6);
    run_ldouble(cplus_strtold, strtold, "strtold", "1e-99999", 0.0L, ERANGE, 8);
}

/* ------------------------------------------------------------------ */
/* 08 atoi                                                            */
/* ------------------------------------------------------------------ */
static void m_conv_08_atoi(void) {
    /* METHOD: atoi -- valid/whitespace/sign/invalid, NULL guard,
     * logging parity, host differential. */
    run_atoi("atoi", "123", 123, 0);
    run_atoi("atoi", "  -42", -42, 0);
    run_atoi("atoi", "+77xyz", 77, 0);
    run_atoi("atoi", "abc", 0, EINVAL);
    run_atoi("atoi", "", 0, EINVAL);
    run_atoi("atoi(NULL)", NULL, 0, 0);
}

/* ------------------------------------------------------------------ */
/* 09 atol                                                            */
/* ------------------------------------------------------------------ */
static void m_conv_09_atol(void) {
    /* METHOD: atol -- valid/whitespace/sign/invalid, NULL guard,
     * logging parity, host differential. */
    run_atol("atol", "123", 123L, 0);
    run_atol("atol", "  -42", -42L, 0);
    run_atol("atol", "0x2A", 0L, 0); /* atol is base 10 */
    run_atol("atol", "junk", 0L, EINVAL);
    run_atol("atol(NULL)", NULL, 0L, 0);
}

/* ------------------------------------------------------------------ */
/* 10 atoll                                                           */
/* ------------------------------------------------------------------ */
static void m_conv_10_atoll(void) {
    /* METHOD: atoll -- musl-derived parser; valid/whitespace/sign/invalid,
     * no signed-overflow input (wrapper has no ERANGE contract), logging
     * parity, host differential. */
    run_atoll("atoll", "123", 123LL, 0, 1);
    run_atoll("atoll", "  -42", -42LL, 0, 1);
    run_atoll("atoll", "\t+77xyz", 77LL, 0, 1);
    run_atoll("atoll", "0x2A", 0LL, 0, 1); /* base 10: stops at 'x' */
    run_atoll("atoll", "abc", 0LL, 0, 0);  /* invalid: host adds EINVAL */
    run_atoll("atoll", "", 0LL, 0, 0);     /* invalid: host adds EINVAL */
}

/* ------------------------------------------------------------------ */
/* 11 atof                                                            */
/* ------------------------------------------------------------------ */
static void m_conv_11_atof(void) {
    /* METHOD: atof -- valid/whitespace/sign/invalid, logging parity,
     * host differential (NULL not passed: host atof(NULL) is UB). */
    run_atof("atof", "1.5", 1.5, 0);
    run_atof("atof", "  -2.5xyz", -2.5, 0);
    run_atof("atof", "1e9999", HUGE_VAL, ERANGE);
    run_atof("atof", "abc", 0.0, 0);
}

/* ------------------------------------------------------------------ */
/* Wide strtoimax/strtoumax runners.                                   */
/* ------------------------------------------------------------------ */
typedef intmax_t (*fn_wimax)(const wchar_t *, wchar_t **, int);
typedef uintmax_t (*fn_wumax)(const wchar_t *, wchar_t **, int);

static void run_wimax(fn_wimax cf, fn_wimax hf, const char *nm, const wchar_t *s,
                      int base, int give_end, intmax_t want, int want_errno,
                      ptrdiff_t want_off) {
    wchar_t *e0 = NULL, *e1 = NULL, *eh = NULL;
    intmax_t r0, r1, rh; int n0, n1, nh;
    log_off(); errno = 0; r0 = cf(s, give_end ? &e0 : NULL, base); n0 = errno;
    log_on();  errno = 0; r1 = cf(s, give_end ? &e1 : NULL, base); n1 = errno;
    log_off(); errno = 0; rh = hf(s, give_end ? &eh : NULL, base); nh = errno;
    CHECK(r0 == r1, "%s log parity ret %jd/%jd", nm, r0, r1);
    CHECK(n0 == n1, "%s log parity errno %d/%d", nm, n0, n1);
    if (give_end)
        CHECK(e0 - s == e1 - s, "%s log parity endptr %td/%td", nm,
              e0 - s, e1 - s);
    CHECK(r0 == rh, "%s host diff ret %jd/%jd", nm, r0, rh);
    CHECK(n0 == nh, "%s host diff errno %d/%d", nm, n0, nh);
    if (give_end)
        CHECK(e0 - s == eh - s, "%s host diff endptr %td/%td", nm,
              e0 - s, eh - s);
    CHECK(r0 == want, "%s expected ret %jd got %jd", nm, want, r0);
    if (want_errno >= 0)
        CHECK(n0 == want_errno, "%s expected errno %d got %d", nm, want_errno, n0);
    if (give_end && want_off >= 0)
        CHECK(e0 - s == want_off, "%s expected endptr %td got %td", nm,
              want_off, e0 - s);
}

static void run_wumax(fn_wumax cf, fn_wumax hf, const char *nm, const wchar_t *s,
                      int base, int give_end, uintmax_t want, int want_errno,
                      ptrdiff_t want_off) {
    wchar_t *e0 = NULL, *e1 = NULL, *eh = NULL;
    uintmax_t r0, r1, rh; int n0, n1, nh;
    log_off(); errno = 0; r0 = cf(s, give_end ? &e0 : NULL, base); n0 = errno;
    log_on();  errno = 0; r1 = cf(s, give_end ? &e1 : NULL, base); n1 = errno;
    log_off(); errno = 0; rh = hf(s, give_end ? &eh : NULL, base); nh = errno;
    CHECK(r0 == r1, "%s log parity ret %ju/%ju", nm, r0, r1);
    CHECK(n0 == n1, "%s log parity errno %d/%d", nm, n0, n1);
    if (give_end)
        CHECK(e0 - s == e1 - s, "%s log parity endptr %td/%td", nm,
              e0 - s, e1 - s);
    CHECK(r0 == rh, "%s host diff ret %ju/%ju", nm, r0, rh);
    CHECK(n0 == nh, "%s host diff errno %d/%d", nm, n0, nh);
    if (give_end)
        CHECK(e0 - s == eh - s, "%s host diff endptr %td/%td", nm,
              e0 - s, eh - s);
    CHECK(r0 == want, "%s expected ret %ju got %ju", nm, want, r0);
    if (want_errno >= 0)
        CHECK(n0 == want_errno, "%s expected errno %d got %d", nm, want_errno, n0);
    if (give_end && want_off >= 0)
        CHECK(e0 - s == want_off, "%s expected endptr %td got %td", nm,
              want_off, e0 - s);
}

/* ------------------------------------------------------------------ */
/* 12 strtoimax                                                       */
/* ------------------------------------------------------------------ */
static void m_conv_12_strtoimax(void) {
    /* METHOD: strtoimax -- decimal/hex/octal, whitespace/sign, invalid+endptr,
     * ERANGE overflow, bad bases, NULL guard, logging parity. */
    run_imax(cplus_strtoimax, strtoimax, "strtoimax", "123", 10, 1,
             (intmax_t)123, 0, 3);
    run_imax(cplus_strtoimax, strtoimax, "strtoimax", "  -42", 10, 1,
             (intmax_t)-42, 0, 5);
    run_imax(cplus_strtoimax, strtoimax, "strtoimax", "0x2A", 0, 1,
             (intmax_t)42, 0, 4);
    run_imax(cplus_strtoimax, strtoimax, "strtoimax", "052", 0, 1,
             (intmax_t)42, 0, 3);
    run_imax(cplus_strtoimax, strtoimax, "strtoimax", "abc", 10, 1,
             (intmax_t)0, EINVAL, 0);
    run_imax(cplus_strtoimax, strtoimax, "strtoimax", "9223372036854775808", 10, 1,
             INTMAX_MAX, ERANGE, 19);
    run_imax(cplus_strtoimax, strtoimax, "strtoimax", "1", 1, 1,
             (intmax_t)0, EINVAL, 0);
    run_imax(cplus_strtoimax, strtoimax, "strtoimax", "1", 37, 1,
             (intmax_t)0, EINVAL, 0);
    run_imax(cplus_strtoimax, strtoimax, "strtoimax", "1", INT_MIN, 1,
             (intmax_t)0, EINVAL, 0);

    /* NULL input is a documented hardening extension: no host differential. */
    {
        char *e0 = (char *)1, *e1 = (char *)1;
        intmax_t r0, r1; int n0, n1;
        log_off(); errno = 0; r0 = cplus_strtoimax(NULL, &e0, 10); n0 = errno;
        log_on();  errno = 0; r1 = cplus_strtoimax(NULL, &e1, 10); n1 = errno;
        log_off();
        CHECK(r0 == 0 && r1 == 0, "strtoimax(NULL) ret %jd/%jd", r0, r1);
        CHECK(e0 == NULL && e1 == NULL, "strtoimax(NULL) endptr not NULL");
        CHECK(n0 == EINVAL && n1 == EINVAL, "strtoimax(NULL) errno %d/%d", n0, n1);
    }
}

/* ------------------------------------------------------------------ */
/* 13 strtoumax                                                       */
/* ------------------------------------------------------------------ */
static void m_conv_13_strtoumax(void) {
    /* METHOD: strtoumax -- decimal/hex/octal, whitespace/sign, invalid+endptr,
     * ERANGE overflow, bad bases, NULL guard, logging parity. */
    run_umax(cplus_strtoumax, strtoumax, "strtoumax", "123", 10, 1,
             (uintmax_t)123, 0, 3);
    run_umax(cplus_strtoumax, strtoumax, "strtoumax", "  +42", 10, 1,
             (uintmax_t)42, 0, 5);
    run_umax(cplus_strtoumax, strtoumax, "strtoumax", "0xFF", 0, 1,
             (uintmax_t)255, 0, 4);
    run_umax(cplus_strtoumax, strtoumax, "strtoumax", "077", 0, 1,
             (uintmax_t)63, 0, 3);
    run_umax(cplus_strtoumax, strtoumax, "strtoumax", "xyz", 10, 1,
             (uintmax_t)0, EINVAL, 0);
    run_umax(cplus_strtoumax, strtoumax, "strtoumax", "184467440737095516160", 10, 1,
             UINTMAX_MAX, ERANGE, 21);
    run_umax(cplus_strtoumax, strtoumax, "strtoumax", "1", 1, 1,
             (uintmax_t)0, EINVAL, 0);
    run_umax(cplus_strtoumax, strtoumax, "strtoumax", "1", 37, 1,
             (uintmax_t)0, EINVAL, 0);
    run_umax(cplus_strtoumax, strtoumax, "strtoumax", "1", INT_MIN, 1,
             (uintmax_t)0, EINVAL, 0);
    {
        char *e0 = (char *)1, *e1 = (char *)1;
        uintmax_t r0, r1; int n0, n1;
        log_off(); errno = 0; r0 = cplus_strtoumax(NULL, &e0, 10); n0 = errno;
        log_on();  errno = 0; r1 = cplus_strtoumax(NULL, &e1, 10); n1 = errno;
        log_off();
        CHECK(r0 == 0 && r1 == 0, "strtoumax(NULL) ret %ju/%ju", r0, r1);
        CHECK(e0 == NULL && e1 == NULL, "strtoumax(NULL) endptr not NULL");
        CHECK(n0 == EINVAL && n1 == EINVAL, "strtoumax(NULL) errno %d/%d", n0, n1);
    }
}

/* ------------------------------------------------------------------ */
/* 14 wcstoimax                                                       */
/* ------------------------------------------------------------------ */
static void m_conv_14_wcstoimax(void) {
    /* METHOD: wcstoimax -- wide decimal/hex/octal, whitespace/sign,
     * invalid+endptr, ERANGE overflow, bad bases, NULL guard, logging parity. */
    run_wimax(cplus_wcstoimax, wcstoimax, "wcstoimax", L"123", 10, 1,
              (intmax_t)123, 0, 3);
    run_wimax(cplus_wcstoimax, wcstoimax, "wcstoimax", L"  -42", 10, 1,
              (intmax_t)-42, 0, 5);
    run_wimax(cplus_wcstoimax, wcstoimax, "wcstoimax", L"0x2A", 0, 1,
              (intmax_t)42, 0, 4);
    run_wimax(cplus_wcstoimax, wcstoimax, "wcstoimax", L"052", 0, 1,
              (intmax_t)42, 0, 3);
    run_wimax(cplus_wcstoimax, wcstoimax, "wcstoimax", L"abc", 10, 1,
              (intmax_t)0, EINVAL, 0);
    run_wimax(cplus_wcstoimax, wcstoimax, "wcstoimax", L"123xyz", 10, 1,
              (intmax_t)123, 0, 3);
    run_wimax(cplus_wcstoimax, wcstoimax, "wcstoimax", L"9999999999999999999999999", 10, 1,
              INTMAX_MAX, ERANGE, 25);
    run_wimax(cplus_wcstoimax, wcstoimax, "wcstoimax", L"1", 1, 1,
              (intmax_t)0, EINVAL, 0);
    run_wimax(cplus_wcstoimax, wcstoimax, "wcstoimax", L"1", 37, 1,
              (intmax_t)0, EINVAL, 0);
    run_wimax(cplus_wcstoimax, wcstoimax, "wcstoimax", L"1", INT_MIN, 1,
              (intmax_t)0, EINVAL, 0);
    {
        wchar_t *e0 = (wchar_t *)1, *e1 = (wchar_t *)1;
        intmax_t r0, r1; int n0, n1;
        log_off(); errno = 0; r0 = cplus_wcstoimax(NULL, &e0, 10); n0 = errno;
        log_on();  errno = 0; r1 = cplus_wcstoimax(NULL, &e1, 10); n1 = errno;
        log_off();
        CHECK(r0 == 0 && r1 == 0, "wcstoimax(NULL) ret %jd/%jd", r0, r1);
        CHECK(e0 == NULL && e1 == NULL, "wcstoimax(NULL) endptr not NULL");
        CHECK(n0 == EINVAL && n1 == EINVAL, "wcstoimax(NULL) errno %d/%d", n0, n1);
    }
}

/* ------------------------------------------------------------------ */
/* 15 wcstoumax                                                       */
/* ------------------------------------------------------------------ */
static void m_conv_15_wcstoumax(void) {
    /* METHOD: wcstoumax -- wide decimal/hex/octal, whitespace/sign,
     * invalid+endptr, ERANGE overflow, bad bases, NULL guard, logging parity. */
    run_wumax(cplus_wcstoumax, wcstoumax, "wcstoumax", L"123", 10, 1,
              (uintmax_t)123, 0, 3);
    run_wumax(cplus_wcstoumax, wcstoumax, "wcstoumax", L"  +42", 10, 1,
              (uintmax_t)42, 0, 5);
    run_wumax(cplus_wcstoumax, wcstoumax, "wcstoumax", L"0xFF", 0, 1,
              (uintmax_t)255, 0, 4);
    run_wumax(cplus_wcstoumax, wcstoumax, "wcstoumax", L"077", 0, 1,
              (uintmax_t)63, 0, 3);
    run_wumax(cplus_wcstoumax, wcstoumax, "wcstoumax", L"zzz", 10, 1,
              (uintmax_t)0, EINVAL, 0);
    run_wumax(cplus_wcstoumax, wcstoumax, "wcstoumax", L"184467440737095516160", 10, 1,
              UINTMAX_MAX, ERANGE, 21);
    run_wumax(cplus_wcstoumax, wcstoumax, "wcstoumax", L"1", 1, 1,
              (uintmax_t)0, EINVAL, 0);
    run_wumax(cplus_wcstoumax, wcstoumax, "wcstoumax", L"1", 37, 1,
              (uintmax_t)0, EINVAL, 0);
    run_wumax(cplus_wcstoumax, wcstoumax, "wcstoumax", L"1", INT_MIN, 1,
              (uintmax_t)0, EINVAL, 0);
    {
        wchar_t *e0 = (wchar_t *)1, *e1 = (wchar_t *)1;
        uintmax_t r0, r1; int n0, n1;
        log_off(); errno = 0; r0 = cplus_wcstoumax(NULL, &e0, 10); n0 = errno;
        log_on();  errno = 0; r1 = cplus_wcstoumax(NULL, &e1, 10); n1 = errno;
        log_off();
        CHECK(r0 == 0 && r1 == 0, "wcstoumax(NULL) ret %ju/%ju", r0, r1);
        CHECK(e0 == NULL && e1 == NULL, "wcstoumax(NULL) endptr not NULL");
        CHECK(n0 == EINVAL && n1 == EINVAL, "wcstoumax(NULL) errno %d/%d", n0, n1);
    }
}

/* ------------------------------------------------------------------ */
/* 16 imaxabs                                                         */
/* ------------------------------------------------------------------ */
static void m_conv_16_imaxabs(void) {
    /* METHOD: imaxabs -- positive/negative/zero (INTMAX_MIN deliberately not
     * used: it has no representable absolute value), logging parity,
     * host differential. */
    const intmax_t vals[] = { 0, 1, -1, 12345, -12345, INTMAX_MAX, -(INTMAX_MAX - 1) };
    size_t i;
    for (i = 0; i < sizeof vals / sizeof vals[0]; ++i) {
        intmax_t r0, r1, rh;
        log_off(); errno = 0; r0 = cplus_imaxabs(vals[i]);
        log_on();  errno = 0; r1 = cplus_imaxabs(vals[i]);
        log_off(); errno = 0; rh = imaxabs(vals[i]);
        CHECK(r0 == r1, "imaxabs(%jd) log parity %jd/%jd", vals[i], r0, r1);
        CHECK(r0 == rh, "imaxabs(%jd) host diff %jd/%jd", vals[i], r0, rh);
        CHECK(r0 == (vals[i] < 0 ? -vals[i] : vals[i]),
              "imaxabs(%jd) expected value %jd", vals[i], r0);
    }
}

/* ------------------------------------------------------------------ */
/* 17 imaxdiv                                                         */
/* ------------------------------------------------------------------ */
static void m_conv_17_imaxdiv(void) {
    /* METHOD: imaxdiv -- positive/negative quotient and remainder; zero
     * divisor excluded (genuine UB), logging parity, host differential. */
    const intmax_t pairs[][2] = {
        { 17, 5 }, { -17, 5 }, { 17, -5 }, { -17, -5 }, { 0, 7 }, { 100, 100 }
    };
    size_t i;
    for (i = 0; i < sizeof pairs / sizeof pairs[0]; ++i) {
        intmax_t n = pairs[i][0], d = pairs[i][1];
        imaxdiv_t a, b, h;
        log_off(); errno = 0; a = cplus_imaxdiv(n, d);
        log_on();  errno = 0; b = cplus_imaxdiv(n, d);
        log_off(); errno = 0; h = imaxdiv(n, d);
        CHECK(a.quot == b.quot && a.rem == b.rem,
              "imaxdiv(%jd,%jd) log parity {%jd,%jd}/{%jd,%jd}",
              n, d, a.quot, a.rem, b.quot, b.rem);
        CHECK(a.quot == h.quot && a.rem == h.rem,
              "imaxdiv(%jd,%jd) host diff {%jd,%jd}/{%jd,%jd}",
              n, d, a.quot, a.rem, h.quot, h.rem);
        CHECK(a.quot == n / d && a.rem == n % d,
              "imaxdiv(%jd,%jd) expected {%jd,%jd}", n, d, n / d, n % d);
    }
}

/* ------------------------------------------------------------------ */
/* 18 div / ldiv / lldiv                                              */
/* ------------------------------------------------------------------ */
static void m_conv_18_div_ldiv_lldiv(void) {
    /* METHOD: div/ldiv/lldiv -- positive/negative quotient and remainder;
     * zero divisor excluded, logging parity, host differential. */
    const int ip[][2] = { { 17, 5 }, { -17, 5 }, { 17, -5 }, { -17, -5 }, { 0, 7 } };
    const long lp[][2] = { { 1000000L, 7L }, { -1000000L, 7L }, { 5L, -2L } };
    const long long llp[][2] = { { 10000000000LL, 3LL }, { -10000000000LL, 3LL }, { 7LL, -7LL } };
    size_t i;
    for (i = 0; i < sizeof ip / sizeof ip[0]; ++i) {
        div_t a, b, h;
        log_off(); a = cplus_div(ip[i][0], ip[i][1]);
        log_on();  b = cplus_div(ip[i][0], ip[i][1]);
        log_off(); h = div(ip[i][0], ip[i][1]);
        CHECK(a.quot == b.quot && a.rem == b.rem,
              "div log parity %d/%d", a.quot, b.quot);
        CHECK(a.quot == h.quot && a.rem == h.rem,
              "div host diff %d/%d", a.quot, h.quot);
        CHECK(a.quot == ip[i][0] / ip[i][1] && a.rem == ip[i][0] % ip[i][1],
              "div expected {%d,%d}", a.quot, a.rem);
    }
    for (i = 0; i < sizeof lp / sizeof lp[0]; ++i) {
        ldiv_t a, b, h;
        log_off(); a = cplus_ldiv(lp[i][0], lp[i][1]);
        log_on();  b = cplus_ldiv(lp[i][0], lp[i][1]);
        log_off(); h = ldiv(lp[i][0], lp[i][1]);
        CHECK(a.quot == b.quot && a.rem == b.rem,
              "ldiv log parity %ld/%ld", a.quot, b.quot);
        CHECK(a.quot == h.quot && a.rem == h.rem,
              "ldiv host diff %ld/%ld", a.quot, h.quot);
        CHECK(a.quot == lp[i][0] / lp[i][1] && a.rem == lp[i][0] % lp[i][1],
              "ldiv expected {%ld,%ld}", a.quot, a.rem);
    }
    for (i = 0; i < sizeof llp / sizeof llp[0]; ++i) {
        lldiv_t a, b, h;
        log_off(); a = cplus_lldiv(llp[i][0], llp[i][1]);
        log_on();  b = cplus_lldiv(llp[i][0], llp[i][1]);
        log_off(); h = lldiv(llp[i][0], llp[i][1]);
        CHECK(a.quot == b.quot && a.rem == b.rem,
              "lldiv log parity %lld/%lld", a.quot, b.quot);
        CHECK(a.quot == h.quot && a.rem == h.rem,
              "lldiv host diff %lld/%lld", a.quot, h.quot);
        CHECK(a.quot == llp[i][0] / llp[i][1] && a.rem == llp[i][0] % llp[i][1],
              "lldiv expected {%lld,%lld}", a.quot, a.rem);
    }
}

/* ------------------------------------------------------------------ */
/* 19 abs / labs / llabs                                              */
/* ------------------------------------------------------------------ */
static void m_conv_19_abs_labs_llabs(void) {
    /* METHOD: abs/labs/llabs -- zero/positive/negative and maximum
     * magnitudes with representable negation; MIN values excluded to avoid
     * signed-overflow UB in both wrapper and host; logging parity + host diff. */
    const int iv[] = { 0, 1, -1, 12345, -12345, INT_MAX, -(INT_MAX - 1) };
    const long lv[] = { 0L, 1L, -1L, 1234567L, -1234567L, LONG_MAX, -(LONG_MAX - 1) };
    const long long llv[] = { 0LL, 1LL, -1LL, 1234567890123LL, -1234567890123LL,
                              LLONG_MAX, -(LLONG_MAX - 1) };
    size_t i;
    for (i = 0; i < sizeof iv / sizeof iv[0]; ++i) {
        int r0, r1, rh;
        log_off(); r0 = cplus_abs(iv[i]);
        log_on();  r1 = cplus_abs(iv[i]);
        log_off(); rh = abs(iv[i]);
        CHECK(r0 == r1, "abs(%d) log parity %d/%d", iv[i], r0, r1);
        CHECK(r0 == rh, "abs(%d) host diff %d/%d", iv[i], r0, rh);
        CHECK(r0 == (iv[i] < 0 ? -iv[i] : iv[i]), "abs(%d) expected %d", iv[i], r0);
    }
    for (i = 0; i < sizeof lv / sizeof lv[0]; ++i) {
        long r0, r1, rh;
        log_off(); r0 = cplus_labs(lv[i]);
        log_on();  r1 = cplus_labs(lv[i]);
        log_off(); rh = labs(lv[i]);
        CHECK(r0 == r1, "labs(%ld) log parity %ld/%ld", lv[i], r0, r1);
        CHECK(r0 == rh, "labs(%ld) host diff %ld/%ld", lv[i], r0, rh);
        CHECK(r0 == (lv[i] < 0 ? -lv[i] : lv[i]), "labs(%ld) expected %ld", lv[i], r0);
    }
    for (i = 0; i < sizeof llv / sizeof llv[0]; ++i) {
        long long r0, r1, rh;
        log_off(); r0 = cplus_llabs(llv[i]);
        log_on();  r1 = cplus_llabs(llv[i]);
        log_off(); rh = llabs(llv[i]);
        CHECK(r0 == r1, "llabs(%lld) log parity %lld/%lld", llv[i], r0, r1);
        CHECK(r0 == rh, "llabs(%lld) host diff %lld/%lld", llv[i], r0, rh);
        CHECK(r0 == (llv[i] < 0 ? -llv[i] : llv[i]),
              "llabs(%lld) expected %lld", llv[i], r0);
    }
}

/* ------------------------------------------------------------------ */
/* 20 all cplus_is* byte ctype predicates (+ isascii/toascii)          */
/* ------------------------------------------------------------------ */
typedef int (*fn_pred)(int);

static void run_pred_byte(const char *nm, fn_pred cf, int (*hf)(int), int c,
                          int want) {
    int r0, r1, rh;
    log_off(); r0 = cf(c);
    log_on();  r1 = cf(c);
    log_off(); rh = hf ? ((c == EOF) ? hf(EOF) : hf((unsigned char)c)) : want;
    CHECK(r0 == r1, "%s(%d) log parity %d/%d", nm, c, r0, r1);
    if (hf) CHECK(r0 == rh, "%s(%d) host diff %d/%d", nm, c, r0, rh);
    CHECK(r0 == want, "%s(%d) expected %d got %d", nm, c, want, r0);
}

static void m_conv_20_ctype_predicates(void) {
    /* METHOD: all cplus_is* byte predicates (isalpha/isdigit/isspace/isalnum/
     * isupper/islower/isxdigit/isprint/isgraph/ispunct/iscntrl/isblank/isascii)
     * and toascii; representative bytes, EOF, logging parity, host differential. */
    static const struct {
        const char *nm;
        fn_pred cf;
        int (*hf)(int);
    } tabs[] = {
        { "isalpha", cplus_isalpha, isalpha },
        { "isdigit", cplus_isdigit, isdigit },
        { "isspace", cplus_isspace, isspace },
        { "isalnum", cplus_isalnum, isalnum },
        { "isupper", cplus_isupper, isupper },
        { "islower", cplus_islower, islower },
        { "isxdigit", cplus_isxdigit, isxdigit },
        { "isprint", cplus_isprint, isprint },
        { "isgraph", cplus_isgraph, isgraph },
        { "ispunct", cplus_ispunct, ispunct },
        { "iscntrl", cplus_iscntrl, iscntrl },
        { "isblank", cplus_isblank, isblank },
        { "isascii", cplus_isascii, NULL }
    };
    static const int chars[] = {
        'a', 'Z', '0', '9', ' ', '\t', '\n', '\r', '!', '~', '@',
        0x7f, 0x80, 0xff, EOF
    };
    static const int ascii_want[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0
    };
    size_t t, i;
    for (t = 0; t < sizeof tabs / sizeof tabs[0]; ++t) {
        for (i = 0; i < sizeof chars / sizeof chars[0]; ++i) {
            int c = chars[i];
            int want;
            if (tabs[t].hf) {
                want = (c == EOF) ? tabs[t].hf(EOF)
                                  : tabs[t].hf((unsigned char)c);
            } else {
                want = ascii_want[i]; /* isascii: !(c & ~0x7f) */
            }
            run_pred_byte(tabs[t].nm, tabs[t].cf, tabs[t].hf, c, want);
        }
    }
    /* Explicit anchor assertions for the byte predicates. */
    CHECK(cplus_isalpha('a') && !cplus_isalpha('0'), "isalpha anchor");
    CHECK(cplus_isdigit('0') && !cplus_isdigit('a'), "isdigit anchor");
    CHECK(cplus_isspace(' ') && cplus_isspace('\n') && !cplus_isspace('x'),
          "isspace anchor");
    CHECK(cplus_isalnum('Z') && !cplus_isalnum('!'), "isalnum anchor");
    CHECK(cplus_isupper('A') && !cplus_isupper('a'), "isupper anchor");
    CHECK(cplus_islower('a') && !cplus_islower('A'), "islower anchor");
    CHECK(cplus_isxdigit('f') && cplus_isxdigit('9') && !cplus_isxdigit('g'),
          "isxdigit anchor");
    CHECK(cplus_isprint('~') && !cplus_isprint('\n'), "isprint anchor");
    CHECK(cplus_isgraph('~') && !cplus_isgraph(' '), "isgraph anchor");
    CHECK(cplus_ispunct('!') && !cplus_ispunct('a'), "ispunct anchor");
    CHECK(cplus_iscntrl('\n') && !cplus_iscntrl('a'), "iscntrl anchor");
    CHECK(cplus_isblank(' ') && cplus_isblank('\t') && !cplus_isblank('\n'),
          "isblank anchor");
    CHECK(cplus_isascii('a') && !cplus_isascii(0x80) && !cplus_isascii(EOF),
          "isascii anchor");
    /* toascii: mask low 7 bits; no host equivalent, parity + expectation. */
    {
        const int tc[] = { 0, 'a', 0x7f, 0x80, 0x1ff, EOF };
        const int tw[] = { 0, 'a', 0x7f, 0x00, 0x7f, 0x7f };
        size_t k;
        for (k = 0; k < sizeof tc / sizeof tc[0]; ++k) {
            int r0, r1;
            log_off(); r0 = cplus_toascii(tc[k]);
            log_on();  r1 = cplus_toascii(tc[k]);
            log_off();
            CHECK(r0 == r1, "toascii(%d) log parity %d/%d", tc[k], r0, r1);
            CHECK(r0 == tw[k], "toascii(%d) expected %d got %d", tc[k], tw[k], r0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* 21 toupper / tolower (including EOF)                               */
/* ------------------------------------------------------------------ */
static void m_conv_21_toupper_tolower(void) {
    /* METHOD: toupper/tolower -- representative bytes plus EOF (must map to
     * EOF), logging parity, host differential on valid inputs. */
    static const int chars[] = { 'a', 'Z', '0', ' ', '!', '~', 0x7f, 0x80, 0xff, EOF };
    size_t i;
    for (i = 0; i < sizeof chars / sizeof chars[0]; ++i) {
        int c = chars[i];
        int u0, u1, uh, l0, l1, lh;
        log_off(); u0 = cplus_toupper(c);
        log_on();  u1 = cplus_toupper(c);
        log_off(); l0 = cplus_tolower(c);
        log_on();  l1 = cplus_tolower(c);
        log_off();
        uh = (c == EOF) ? EOF : toupper((unsigned char)c);
        lh = (c == EOF) ? EOF : tolower((unsigned char)c);
        CHECK(u0 == u1, "toupper(%d) log parity %d/%d", c, u0, u1);
        CHECK(l0 == l1, "tolower(%d) log parity %d/%d", c, l0, l1);
        CHECK(u0 == uh, "toupper(%d) host diff %d/%d", c, u0, uh);
        CHECK(l0 == lh, "tolower(%d) host diff %d/%d", c, l0, lh);
    }
    CHECK(cplus_toupper('a') == 'A', "toupper('a') anchor");
    CHECK(cplus_tolower('A') == 'a', "tolower('A') anchor");
    CHECK(cplus_toupper(EOF) == EOF, "toupper(EOF) must return EOF");
    CHECK(cplus_tolower(EOF) == EOF, "tolower(EOF) must return EOF");
}

/* ------------------------------------------------------------------ */
/* 22 wctype / wctrans / towupper / towlower (+ iswctype/towctrans,     */
/*     and all cplus_isw* wide predicates)                             */
/* ------------------------------------------------------------------ */
static void run_pred_wide(const char *nm, int (*cf)(wint_t), int (*hf)(wint_t),
                          wint_t wc) {
    int r0, r1, rh;
    log_off(); r0 = cf(wc);
    log_on();  r1 = cf(wc);
    log_off(); rh = hf(wc) != 0;
    CHECK(r0 == r1, "%s(%ld) log parity %d/%d", nm, (long)wc, r0, r1);
    CHECK(r0 == rh, "%s(%ld) host diff %d/%d", nm, (long)wc, r0, rh);
}

static void m_conv_22_wctype_wctrans_tow(void) {
    /* METHOD: wctype/wctrans/towupper/towlower (+ iswctype, towctrans and all
     * cplus_isw* predicates) -- valid descriptors, WEOF, NULL guards,
     * logging parity, host differential. */
    static const struct {
        const char *nm;
        int (*cf)(wint_t);
        int (*hf)(wint_t);
    } tabs[] = {
        { "iswalpha", cplus_iswalpha, iswalpha },
        { "iswdigit", cplus_iswdigit, iswdigit },
        { "iswspace", cplus_iswspace, iswspace },
        { "iswalnum", cplus_iswalnum, iswalnum },
        { "iswupper", cplus_iswupper, iswupper },
        { "iswlower", cplus_iswlower, iswlower },
        { "iswxdigit", cplus_iswxdigit, iswxdigit },
        { "iswprint", cplus_iswprint, iswprint },
        { "iswgraph", cplus_iswgraph, iswgraph },
        { "iswpunct", cplus_iswpunct, iswpunct },
        { "iswcntrl", cplus_iswcntrl, iswcntrl },
        { "iswblank", cplus_iswblank, iswblank }
    };
    static const wint_t wcs[] = { L'a', L'Z', L'0', L' ', L'\t', L'\n',
                                  L'!', 0x00e9, 0x03b1, WEOF };
    size_t t, i;

    for (t = 0; t < sizeof tabs / sizeof tabs[0]; ++t)
        for (i = 0; i < sizeof wcs / sizeof wcs[0]; ++i)
            run_pred_wide(tabs[t].nm, tabs[t].cf, tabs[t].hf, wcs[i]);

    /* towupper / towlower, including WEOF. */
    for (i = 0; i < sizeof wcs / sizeof wcs[0]; ++i) {
        wint_t u0, u1, uh, l0, l1, lh;
        log_off(); u0 = cplus_towupper(wcs[i]);
        log_on();  u1 = cplus_towupper(wcs[i]);
        log_off(); l0 = cplus_towlower(wcs[i]);
        log_on();  l1 = cplus_towlower(wcs[i]);
        log_off(); uh = towupper(wcs[i]); lh = towlower(wcs[i]);
        CHECK(u0 == u1, "towupper(%ld) log parity", (long)wcs[i]);
        CHECK(l0 == l1, "towlower(%ld) log parity", (long)wcs[i]);
        CHECK(u0 == uh, "towupper(%ld) host diff %ld/%ld", (long)wcs[i],
              (long)u0, (long)uh);
        CHECK(l0 == lh, "towlower(%ld) host diff %ld/%ld", (long)wcs[i],
              (long)l0, (long)lh);
    }
    CHECK(cplus_towupper(L'a') == L'A', "towupper(L'a') anchor");
    CHECK(cplus_towlower(L'A') == L'a', "towlower(L'A') anchor");
    CHECK(cplus_towupper(WEOF) == WEOF, "towupper(WEOF) must be WEOF");
    CHECK(cplus_towlower(WEOF) == WEOF, "towlower(WEOF) must be WEOF");

    /* wctype + iswctype with a valid descriptor. */
    {
        wctype_t d0, d1, dh;
        log_off(); errno = 0; d0 = cplus_wctype("alpha");
        log_on();  errno = 0; d1 = cplus_wctype("alpha");
        log_off(); errno = 0; dh = wctype("alpha");
        CHECK(d0 != (wctype_t)0 && d1 == d0, "wctype(\"alpha\") log parity");
        CHECK(d0 == dh, "wctype(\"alpha\") host diff");
        for (i = 0; i < sizeof wcs / sizeof wcs[0]; ++i) {
            int r0, r1, rh;
            log_off(); r0 = cplus_iswctype(wcs[i], d0);
            log_on();  r1 = cplus_iswctype(wcs[i], d0);
            log_off(); rh = iswctype(wcs[i], d0) != 0;
            CHECK(r0 == r1, "iswctype(%ld,alpha) log parity %d/%d",
                  (long)wcs[i], r0, r1);
            CHECK(r0 == rh, "iswctype(%ld,alpha) host diff %d/%d",
                  (long)wcs[i], r0, rh);
        }
        log_off(); errno = 0; d0 = cplus_wctype(NULL);
        log_on();  errno = 0; d1 = cplus_wctype(NULL);
        log_off();
        CHECK(d0 == (wctype_t)0 && d1 == d0, "wctype(NULL) must return 0");
        CHECK(errno == EINVAL, "wctype(NULL) errno EINVAL");
        log_off(); d0 = cplus_wctype("no-such-property-cplus-test");
        log_on();  d1 = cplus_wctype("no-such-property-cplus-test");
        log_off(); dh = wctype("no-such-property-cplus-test");
        CHECK(d0 == (wctype_t)0 && d1 == d0, "wctype(invalid) log parity");
        CHECK(d0 == dh, "wctype(invalid) host diff");
    }

    /* wctrans + towctrans with a valid descriptor. */
    {
        wctrans_t t0, t1, th;
        log_off(); errno = 0; t0 = cplus_wctrans("toupper");
        log_on();  errno = 0; t1 = cplus_wctrans("toupper");
        log_off(); errno = 0; th = wctrans("toupper");
        CHECK(t0 != (wctrans_t)0 && t1 == t0, "wctrans(\"toupper\") log parity");
        CHECK(t0 == th, "wctrans(\"toupper\") host diff");
        for (i = 0; i < sizeof wcs / sizeof wcs[0]; ++i) {
            wint_t r0, r1, rh;
            log_off(); r0 = cplus_towctrans(wcs[i], t0);
            log_on();  r1 = cplus_towctrans(wcs[i], t0);
            log_off(); rh = towctrans(wcs[i], t0);
            CHECK(r0 == r1, "towctrans(%ld) log parity", (long)wcs[i]);
            CHECK(r0 == rh, "towctrans(%ld) host diff %ld/%ld", (long)wcs[i],
                  (long)r0, (long)rh);
        }
        log_off(); errno = 0; t0 = cplus_wctrans(NULL);
        log_on();  errno = 0; t1 = cplus_wctrans(NULL);
        log_off();
        CHECK(t0 == (wctrans_t)0 && t1 == t0, "wctrans(NULL) must return 0");
        CHECK(errno == EINVAL, "wctrans(NULL) errno EINVAL");
    }
}

/* ------------------------------------------------------------------ */
int main(void) {
    mute_stderr();
    m_conv_01_strtol();
    m_conv_02_strtoul();
    m_conv_03_strtoll();
    m_conv_04_strtoull();
    m_conv_05_strtof();
    m_conv_06_strtod();
    m_conv_07_strtold();
    m_conv_08_atoi();
    m_conv_09_atol();
    m_conv_10_atoll();
    m_conv_11_atof();
    m_conv_12_strtoimax();
    m_conv_13_strtoumax();
    m_conv_14_wcstoimax();
    m_conv_15_wcstoumax();
    m_conv_16_imaxabs();
    m_conv_17_imaxdiv();
    m_conv_18_div_ldiv_lldiv();
    m_conv_19_abs_labs_llabs();
    m_conv_20_ctype_predicates();
    m_conv_21_toupper_tolower();
    m_conv_22_wctype_wctrans_tow();
    log_off();
    unmute_stderr();
    printf("methods_conversion: %d/%d checks ok\n", g_checks - g_fail, g_checks);
    return 0;
}
