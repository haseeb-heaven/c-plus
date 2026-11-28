/* test_c23_hypot16.c -- deterministic standalone tests for the additive C23
 * math APIs in libc/src/math/c23_hypot16.c.
 *
 * Build and run (from the repository root, timeout is part of the contract):
 *   /usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
 *     -Ilibc/install-include \
 *     libc/src/math/c23_hypot16.c tests/libc/test_c23_hypot16.c -lm \
 *     -o /tmp/test_c23_hypot16
 *   timeout 30 /tmp/test_c23_hypot16
 *
 * Output: "c23_hypot16: <checks> <failures>" followed by an ok/FAILED line.
 * Exit status is non-zero if any check fails. No result is treated as a pass
 * without an explicit expected value or a deterministic host oracle.
 */
#include <float.h>
#include <math.h>
#include <stdio.h>

#include "cplus_c23_hypot16.h"

static int g_checks = 0;
static int g_failures = 0;

static void check(int cond, const char *what) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        printf("FAIL: %s\n", what);
    }
}

static int close_rel(double got, double want, double rel) {
    if (isnan(got) || isnan(want)) return isnan(got) && isnan(want);
    if (isinf(got) || isinf(want)) return got == want;
    return fabs(got - want) <= rel * (fabs(want) > 1.0 ? fabs(want) : 1.0);
}

static void test_hypot3(void) {
    double r;
    int i;

    /* Exact Pythagorean triples: deterministic integer expectations. */
    check(close_rel(cplus_c23_hypot3(3.0, 4.0, 0.0), 5.0, 0.0), "hypot3 3,4,0 -> 5");
    check(close_rel(cplus_c23_hypot3(3.0, 4.0, 12.0), 13.0, 0.0), "hypot3 3,4,12 -> 13");
    check(close_rel(cplus_c23_hypot3(5.0, 12.0, 0.0), 13.0, 0.0), "hypot3 5,12,0 -> 13");
    check(close_rel(cplus_c23_hypot3(-3.0, -4.0, 12.0), 13.0, 0.0), "hypot3 signed inputs");
    check(cplus_c23_hypot3(0.0, 0.0, 0.0) == 0.0, "hypot3 all zero");

    /* sqrt(3) and sqrt(338): fixed irrational expectations, tight tolerance. */
    check(close_rel(cplus_c23_hypot3(1.0, 1.0, 1.0), sqrt(3.0), 1e-15),
          "hypot3 1,1,1 -> sqrt(3)");
    check(close_rel(cplus_c23_hypot3(5.0, 12.0, 13.0), sqrt(338.0), 1e-15),
          "hypot3 5,12,13 -> sqrt(338)");

    /* Overflow-safe scaling: a naive sqrt(x*x+y*y+z*z) produces inf here
     * because 1e400 is not representable. The true norm is ~1.732e200. */
    r = cplus_c23_hypot3(1e200, 1e200, 1e200);
    check(isfinite(r), "hypot3 1e200 triple stays finite (no naive overflow)");
    check(close_rel(r, sqrt(3.0) * 1e200, 1e-14), "hypot3 1e200 triple value");

    /* Underflow-safe scaling: the naive form squares to 0. */
    r = cplus_c23_hypot3(1e-200, 1e-200, 1e-200);
    check(r > 0.0 && isfinite(r), "hypot3 1e-200 triple stays nonzero");
    check(close_rel(r, sqrt(3.0) * 1e-200, 1e-14), "hypot3 1e-200 triple value");

    /* Range endpoints and special values. */
    check(cplus_c23_hypot3(DBL_MAX, 0.0, 0.0) == DBL_MAX, "hypot3 DBL_MAX single");
    r = cplus_c23_hypot3(DBL_MAX, DBL_MAX, DBL_MAX);
    check(isinf(r) && !isnan(r), "hypot3 DBL_MAX triple -> +inf, not NaN");
    check(isinf(cplus_c23_hypot3(INFINITY, 1.0, 1.0)), "hypot3 one +inf");
    check(isinf(cplus_c23_hypot3(NAN, INFINITY, 1.0)), "hypot3 +inf beats NaN");
    check(isnan(cplus_c23_hypot3(NAN, 1.0, 1.0)), "hypot3 NaN propagation");
    /* DBL_TRUE_MIN is C11; derive the smallest subnormal in C99 terms. */
    {
        double tiny = ldexp(1.0, DBL_MIN_EXP - DBL_MANT_DIG);
        check(cplus_c23_hypot3(tiny, 0.0, 0.0) == tiny,
              "hypot3 smallest subnormal");
    }

    /* Differential sweep against a long-double oracle (tolerance 8 ulp to
     * cover the documented sum-of-squares + sqrt double-rounding). */
    for (i = -12; i <= 12; ++i) {
        double x = i * 0.37, y = i * 0.11 + 1.0, z = -i * 0.73 + 2.0;
        long double ref = sqrtl((long double)x * x + (long double)y * y +
                                (long double)z * z);
        double got = cplus_c23_hypot3(x, y, z);
        check(fabs((long double)got - ref) <= 8.0L * DBL_EPSILON * fabsl(ref),
              "hypot3 differential vs long double");
    }
}

#if defined(__FLT16_MANT_DIG__)
static int f16eq(_Float16 a, _Float16 b) { return (double)a == (double)b; }

static void test_frexp16(void) {
    int e = -999;
    _Float16 r;

    r = cplus_c23_frexp16((_Float16)0.5f, &e);
    check(f16eq(r, (_Float16)0.5f) && e == 0, "frexp16 0.5 -> 0.5*2^0");
    r = cplus_c23_frexp16((_Float16)1.0f, &e);
    check(f16eq(r, (_Float16)0.5f) && e == 1, "frexp16 1 -> 0.5*2^1");
    r = cplus_c23_frexp16((_Float16)3.0f, &e);
    check(f16eq(r, (_Float16)0.75f) && e == 2, "frexp16 3 -> 0.75*2^2");
    r = cplus_c23_frexp16((_Float16)-8.0f, &e);
    check(f16eq(r, (_Float16)-0.5f) && e == 4, "frexp16 -8 -> -0.5*2^4");
    r = cplus_c23_frexp16((_Float16)0.0f, &e);
    check(f16eq(r, (_Float16)0.0f) && e == 0, "frexp16 zero");
    r = cplus_c23_frexp16((_Float16)65504.0f, &e);
    check(f16eq(r, (_Float16)0.99951171875f) && e == 16,
          "frexp16 max finite 65504 -> 0.99951171875*2^16");

    for (e = 1; e <= 48; ++e) {
        _Float16 h = (_Float16)(e * 0.125f);
        int got_e = -999, ref_e = -999;
        _Float16 got = cplus_c23_frexp16(h, &got_e);
        double ref = frexp((double)h, &ref_e);
        check((double)got == ref && got_e == ref_e,
              "frexp16 differential vs double frexp");
    }
}

static void test_fma16(void) {
    _Float16 one = (_Float16)1.0f;
    _Float16 r;

    r = cplus_c23_fma16((_Float16)1.0f, (_Float16)2.0f, (_Float16)3.0f);
    check(f16eq(r, (_Float16)5.0f), "fma16 1*2+3 -> 5");
    r = cplus_c23_fma16((_Float16)-2.0f, (_Float16)3.0f, (_Float16)1.0f);
    check(f16eq(r, (_Float16)-5.0f), "fma16 -2*3+1 -> -5");
    r = cplus_c23_fma16((_Float16)0.5f, (_Float16)0.25f, (_Float16)0.125f);
    check(f16eq(r, (_Float16)0.25f), "fma16 0.5*0.25+0.125 -> 0.25");

    /* Fusion evidence: 1 + 2^-11 is exactly half an f16 ulp at 1.0 and
     * rounds to 1.0 under round-to-nearest-even; 1 + 2^-10 is one full ulp. */
    r = cplus_c23_fma16(one, one, (_Float16)0.00048828125f);
    check(f16eq(r, one), "fma16 fused half-ulp tie -> 1");
    r = cplus_c23_fma16(one, one, (_Float16)0.0009765625f);
    check(f16eq(r, (_Float16)1.0009765625f), "fma16 one-ulp addend");

    {
        int a, b, c;
        for (a = -10; a <= 10; a += 5)
            for (b = -10; b <= 10; b += 5)
                for (c = -10; c <= 10; c += 5) {
                    _Float16 x = (_Float16)(float)a, y = (_Float16)(float)b;
                    _Float16 z = (_Float16)(float)c;
                    double ref = fma((double)x, (double)y, (double)z);
                    check((double)cplus_c23_fma16(x, y, z) == ref,
                          "fma16 differential vs double fma");
                }
    }
}

static void test_logb16(void) {
    int i;
    double vals[] = {1.0, 2.0, 0.5, 3.0, 65504.0, 5.960464477539063e-8};
    double want[] = {0.0, 1.0, -1.0, 1.0, 15.0, -24.0};

    for (i = 0; i < 6; ++i)
        check((double)cplus_c23_logb16((_Float16)(float)vals[i]) == want[i],
              "logb16 fixed value");

    check(isinf((double)cplus_c23_logb16((_Float16)0.0f)), "logb16 zero -> -inf");
    check(isinf((double)cplus_c23_logb16(INFINITY)), "logb16 inf -> +inf");
    check(isnan((double)cplus_c23_logb16(NAN)), "logb16 NaN");

    for (i = 1; i <= 48; ++i) {
        _Float16 h = (_Float16)(i * 0.125f);
        check((double)cplus_c23_logb16(h) == logb((double)h),
              "logb16 differential vs double logb");
    }
}

static void test_exp16(void) {
    int i;
    double x[] = {0.0, 1.0, -1.0, 2.0, -2.0, 0.5};
    double want[] = {1.0, 2.71875, 0.367919921875, 7.390625,
                     0.1353759765625, 1.6484375};

    for (i = 0; i < 6; ++i)
        check(f16eq(cplus_c23_exp16((_Float16)(float)x[i]),
                    (_Float16)(float)want[i]), "exp16 fixed value");

    check(isinf((double)cplus_c23_exp16(INFINITY)), "exp16 +inf");
    check(f16eq(cplus_c23_exp16((_Float16)-INFINITY), (_Float16)0.0f),
          "exp16 -inf -> 0");
    check(isnan((double)cplus_c23_exp16(NAN)), "exp16 NaN");

    /* Deterministic sweep: reference is the host double exp rounded once to
     * binary16, which is IEEE-754 round-to-nearest-even for these inputs. */
    for (i = -32; i <= 32; ++i) {
        _Float16 h = (_Float16)(float)(i * 0.25);
        _Float16 ref = (_Float16)exp((double)h);
        check(f16eq(cplus_c23_exp16(h), ref), "exp16 differential vs double exp");
    }
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
    printf("c23_hypot16: %d %d\n", g_checks, g_failures);
    if (g_failures) {
        printf("c23_hypot16: FAILED\n");
        return 1;
    }
    printf("c23_hypot16: ok\n");
    return 0;
}
