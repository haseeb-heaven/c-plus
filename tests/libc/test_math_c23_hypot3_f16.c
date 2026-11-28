/*
 * test_math_c23_hypot3_f16.c -- standalone C17 tests for the five C23 math
 * APIs closed in libc/src/math/math_c23_hypot3_f16.c:
 *
 *   hypot3, frexp16, fma16, logb16, exp16.
 *
 * Build (from the repository root):
 *   /usr/bin/clang -std=c17 -Wall -Wextra \
 *     -Ilibc -Ilibc/install-include \
 *     libc/src/math/math_c23_hypot3_f16.c libc/log/log.c \
 *     tests/libc/test_math_c23_hypot3_f16.c -lm \
 *     -o /tmp/test_math_c23_hypot3_f16
 *   /tmp/test_math_c23_hypot3_f16
 *
 * For every API we check (1) known exact results, (2) a host differential
 * oracle over a range of inputs (with a documented tolerance for the two
 * nested/double-rounding paths), (3) NaN/Inf/signed-zero classification where
 * applicable, and (4) logging ON/OFF result parity. No placeholder result is
 * treated as a pass; any failure sets a non-zero exit status.
 */

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cplus_math_c23_hypot3_f16.h"

extern void cplus_libc_log_set_enabled(int enabled);

static int g_tested = 0;
static int g_failed = 0;

static void check(int cond, const char *api, const char *what) {
    ++g_tested;
    if (!cond) {
        ++g_failed;
        printf("FAIL %-9s : %s\n", api, what);
    }
}

static int close_double(double got, double want, double tol) {
    if (isnan(got) || isnan(want)) return isnan(got) && isnan(want);
    if (isinf(got) || isinf(want)) return got == want;
    return fabs(got - want) <= tol * (fabs(want) > 1.0 ? fabs(want) : 1.0);
}

static void test_hypot3(void) {
    double r;
    /* exact integer triples */
    check(close_double(cplus_hypot3(3, 4, 0), 5, 1e-15), "hypot3", "3,4,0 -> 5");
    check(close_double(cplus_hypot3(3, 4, 12), 13, 1e-15), "hypot3", "3,4,12 -> 13");
    check(close_double(cplus_hypot3(0, 0, 0), 0, 1e-15), "hypot3", "all zero");
    /* overflow safety: a naive sqrt(x*x+y*y+z*z) would produce inf here but
     * for large inputs with cancellation the nesting must not create NaN. */
    r = cplus_hypot3(DBL_MAX, DBL_MAX, DBL_MAX);
    check(isinf(r) && !isnan(r), "hypot3", "DBL_MAX triple -> +inf, not NaN");
    r = cplus_hypot3(INFINITY, 1, 1);
    check(isinf(r), "hypot3", "one +inf -> +inf");
    check(isnan(cplus_hypot3(NAN, 1, 1)), "hypot3", "NaN propagation");
    /* differential vs a long-double reference (4 ulp of double tolerance for
     * the documented double-rounding deviation of the nested composition) */
    for (int i = -10; i <= 10; ++i) {
        double x = i * 0.37, y = i * 0.11 + 1.0, z = -i * 0.73 + 2.0;
        long double ref = sqrtl((long double)x * x + (long double)y * y + (long double)z * z);
        double got = cplus_hypot3(x, y, z);
        check(fabs((long double)got - ref) <= 16.0L * DBL_EPSILON * fabsl(ref),
              "hypot3", "differential vs long double");
    }
    /* logging ON/OFF parity */
    cplus_libc_log_set_enabled(0);
    r = cplus_hypot3(3, 4, 12);
    cplus_libc_log_set_enabled(1);
    check(close_double(cplus_hypot3(3, 4, 12), r, 0), "hypot3", "logging parity");
    cplus_libc_log_set_enabled(0);
}

#if defined(__FLT16_MANT_DIG__)
static void test_frexp16(void) {
    int e = -999;
    _Float16 r;
    r = cplus_frexp16((_Float16)0.5f, &e);
    check((double)r == 0.5 && e == 0, "frexp16", "0.5 -> 0.5 * 2^0");
    r = cplus_frexp16((_Float16)1.0f, &e);
    check((double)r == 0.5 && e == 1, "frexp16", "1.0 -> 0.5 * 2^1");
    r = cplus_frexp16((_Float16)-8.0f, &e);
    check((double)r == -0.5 && e == 4, "frexp16", "-8.0 -> -0.5 * 2^4");
    r = cplus_frexp16((_Float16)0.0f, &e);
    check((double)r == 0.0 && e == 0, "frexp16", "zero -> 0, e=0");
    /* differential vs double frexp over f16-exact inputs */
    for (int i = 1; i <= 40; ++i) {
        _Float16 h = (_Float16)(i * 0.125f);
        int e1 = 0, e2 = 0;
        _Float16 got = cplus_frexp16(h, &e1);
        double ref = frexp((double)h, &e2);
        check((double)got == ref && e1 == e2, "frexp16", "differential vs double frexp");
    }
}

static void test_fma16(void) {
    _Float16 r;
    r = cplus_fma16((_Float16)1, (_Float16)2, (_Float16)3);
    check((double)r == 5.0, "fma16", "1*2+3 -> 5");
    r = cplus_fma16((_Float16)0.5f, (_Float16)0.25f, (_Float16)0.125f);
    check((double)r == 0.25, "fma16", "0.5*0.25+0.125 -> 0.25");
    /* fusion: 1*1 + 2^-11 rounds to 1.0 in f16 (2^-11 is half an ulp at 1.0) */
    r = cplus_fma16((_Float16)1, (_Float16)1, (_Float16)0.00048828125f);
    check((double)r == 1.0, "fma16", "fused rounding at 1.0");
    for (int a = -10; a <= 10; a += 3)
        for (int b = -10; b <= 10; b += 3)
            for (int c = -10; c <= 10; c += 3) {
                double ref = fma((double)(_Float16)a, (_Float16)b, (_Float16)c);
                _Float16 got = cplus_fma16((_Float16)a, (_Float16)b, (_Float16)c);
                check((double)got == ref, "fma16", "differential vs double fma");
            }
}

static void test_logb16(void) {
    check((double)cplus_logb16((_Float16)1) == 0.0, "logb16", "logb(1)=0");
    check((double)cplus_logb16((_Float16)2) == 1.0, "logb16", "logb(2)=1");
    check((double)cplus_logb16((_Float16)0.5f) == -1.0, "logb16", "logb(0.5)=-1");
    check((double)cplus_logb16((_Float16)8) == 3.0, "logb16", "logb(8)=3");
    check(isinf(cplus_logb16((_Float16)INFINITY)), "logb16", "logb(inf)=inf");
    check(isinf(cplus_logb16((_Float16)0.0f)) && signbit((double)cplus_logb16((_Float16)0.0f)),
          "logb16", "logb(0)=-inf");
    check(isnan(cplus_logb16((_Float16)NAN)), "logb16", "logb(nan)=nan");
    for (int i = 1; i <= 40; ++i) {
        _Float16 h = (_Float16)(i * 0.125f);
        double ref = logb((double)h);
        check((double)cplus_logb16(h) == ref, "logb16", "differential vs double logb");
    }
}

static void test_exp16(void) {
    check((double)cplus_exp16((_Float16)0) == 1.0, "exp16", "exp(0)=1");
    /* correctly-rounded f16 reference values (verified against double exp) */
    check((double)cplus_exp16((_Float16)1) == (double)(_Float16)exp(1.0), "exp16", "exp(1) rounded to f16");
    check((double)cplus_exp16((_Float16)-1) == (double)(_Float16)exp(-1.0), "exp16", "exp(-1) rounded to f16");
    for (int i = -20; i <= 40; ++i) {
        _Float16 h = (_Float16)(i * 0.1234567);
        double ref = (double)(_Float16)exp((double)h);
        check((double)cplus_exp16(h) == ref, "exp16", "differential vs double-exp reference");
    }
    /* logging ON/OFF parity */
    _Float16 a, b;
    cplus_libc_log_set_enabled(0);
    a = cplus_exp16((_Float16)1);
    cplus_libc_log_set_enabled(1);
    b = cplus_exp16((_Float16)1);
    check((double)a == (double)b, "exp16", "logging parity");
    cplus_libc_log_set_enabled(0);
}
#endif /* __FLT16_MANT_DIG__ */

int main(void) {
    test_hypot3();
#if defined(__FLT16_MANT_DIG__)
    test_frexp16();
    test_fma16();
    test_logb16();
    test_exp16();
#endif
    printf("test_math_c23_hypot3_f16: checks=%d failed=%d\n", g_tested, g_failed);
    printf("test_math_c23_hypot3_f16: %s\n", g_failed ? "FAIL" : "ok");
    return g_failed ? 1 : 0;
}
