/*
 * test_math_c23_next.c -- standalone C99 tests for the ISO C23
 * nextup/nextdown families implemented in libc/src/math/math_c23_next.c.
 *
 * Coverage per type (double, float, long double):
 *   (1) host differential -- the cplus_* result equals the host libm
 *       nextafter(x, +/-INFINITY) result bit-for-bit (classification-based
 *       for long double, whose 16-byte representation carries padding);
 *   (2) explicit IEEE anchors that do NOT depend on the wrapper: signed zero,
 *       least/largest subnormal, +/-TYPE_MAX, +/-infinity and NaN payload;
 *   (3) direction and monotonicity: nextup moves toward +inf, nextdown toward
 *       -inf, and both are non-decreasing over a sorted finite input set;
 *   (4) logging ON/OFF parity -- identical results with cplus_libc_log
 *       disabled and enabled.
 *
 * Compile/link commands and captured output are in docs/libc/C23_NEXT_CLOSURE.md.
 */

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cplus_math_c23_next.h"

extern void cplus_libc_log_set_enabled(int);

#define DBL_SUBNORM_MIN (DBL_MIN * DBL_EPSILON)
#define FLT_SUBNORM_MIN (FLT_MIN * FLT_EPSILON)
#define LDBL_SUBNORM_MIN (LDBL_MIN * LDBL_EPSILON)

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, msg, val)                                                   \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!(cond)) {                                                          \
            ++g_failures;                                                       \
            fprintf(stderr, "FAIL line %d: %s (value=%.21Lg)\n",               \
                    __LINE__, (msg), (long double)(val));                       \
        }                                                                       \
    } while (0)

static int bits_equal_d(double a, double b) { return memcmp(&a, &b, sizeof a) == 0; }
static int bits_equal_f(float a, float b) { return memcmp(&a, &b, sizeof a) == 0; }

/* -------------------------------- double --------------------------------- */
static void test_double(void) {
    const double inf = INFINITY;
    const double nan = NAN;
    const double minsub = DBL_SUBNORM_MIN;
    const double maxsub = nextafter(DBL_MIN, 0.0);
    const double inputs[] = {
        -inf, -DBL_MAX, -2.0, -1.0, -minsub, -0.0, 0.0,
        minsub, maxsub, DBL_MIN, 1.0, 2.0, DBL_MAX, inf, nan
    };
    size_t i;
    double nan_up, nan_down;

    for (i = 0; i < sizeof inputs / sizeof inputs[0]; ++i) {
        double x = inputs[i];
        double up_off, up_on, dn_off, dn_on;
        double want_up = nextafter(x, inf);
        double want_dn = nextafter(x, -inf);

        cplus_libc_log_set_enabled(0);
        up_off = cplus_nextup(x);
        dn_off = cplus_nextdown(x);
        cplus_libc_log_set_enabled(1);
        up_on = cplus_nextup(x);
        dn_on = cplus_nextdown(x);
        cplus_libc_log_set_enabled(0);

        CHECK(bits_equal_d(up_off, up_on), "nextup logging parity", x);
        CHECK(bits_equal_d(dn_off, dn_on), "nextdown logging parity", x);
        CHECK(bits_equal_d(up_on, want_up), "nextup host differential", x);
        CHECK(bits_equal_d(dn_on, want_dn), "nextdown host differential", x);

        if (!isnan(x)) {
            CHECK(up_on >= x, "nextup direction (>= x)", x);
            CHECK(dn_on <= x, "nextdown direction (<= x)", x);
            if (isfinite(x)) {
                CHECK(up_on > x, "nextup strictly greater (finite)", x);
                CHECK(dn_on < x, "nextdown strictly less (finite)", x);
            }
        }
    }

    /* Explicit anchors -- independent of the wrapper under test. */
    CHECK(bits_equal_d(cplus_nextup(0.0), minsub), "nextup(+0) -> +minsub", 0.0);
    CHECK(bits_equal_d(cplus_nextup(-0.0), minsub), "nextup(-0) -> +minsub", -0.0);
    CHECK(bits_equal_d(cplus_nextdown(0.0), -minsub), "nextdown(+0) -> -minsub", 0.0);
    CHECK(bits_equal_d(cplus_nextdown(-0.0), -minsub), "nextdown(-0) -> -minsub", -0.0);
    CHECK(cplus_nextup(DBL_MAX) == inf, "nextup(DBL_MAX) -> +inf", DBL_MAX);
    CHECK(cplus_nextdown(-DBL_MAX) == -inf, "nextdown(-DBL_MAX) -> -inf", -DBL_MAX);
    CHECK(cplus_nextup(inf) == inf, "nextup(+inf) -> +inf", inf);
    CHECK(cplus_nextdown(-inf) == -inf, "nextdown(-inf) -> -inf", -inf);
    CHECK(cplus_nextup(-inf) == -DBL_MAX, "nextup(-inf) -> -DBL_MAX", -inf);
    CHECK(cplus_nextdown(inf) == DBL_MAX, "nextdown(+inf) -> DBL_MAX", inf);
    CHECK(cplus_nextup(maxsub) == DBL_MIN, "nextup(max subnormal) -> DBL_MIN", maxsub);
    CHECK(cplus_nextdown(DBL_MIN) == maxsub, "nextdown(DBL_MIN) -> max subnormal", DBL_MIN);
    CHECK(cplus_nextdown(minsub) == 0.0, "nextdown(minsub) -> +0", minsub);
    CHECK(cplus_nextup(1.0) == 1.0 + DBL_EPSILON, "nextup(1.0) anchor", 1.0);
    CHECK(cplus_nextdown(1.0) == nextafter(1.0, 0.0), "nextdown(1.0) anchor", 1.0);

    /* NaN: payload/sign preserved, classification stays NaN. */
    nan_up = cplus_nextup(nan);
    nan_down = cplus_nextdown(nan);
    CHECK(isnan(nan_up), "nextup(NAN) is NaN", nan);
    CHECK(isnan(nan_down), "nextdown(NAN) is NaN", nan);
    CHECK(bits_equal_d(nan_up, nan), "nextup(NAN) preserves bits", nan);
    CHECK(bits_equal_d(nan_down, nan), "nextdown(NAN) preserves bits", nan);

    /* Monotonicity over the sorted finite subset. */
    for (i = 0; i + 1 < sizeof inputs / sizeof inputs[0]; ++i) {
        double a = inputs[i], b = inputs[i + 1];
        if (isnan(a) || isnan(b)) continue;
        CHECK(cplus_nextup(a) <= cplus_nextup(b), "nextup monotonic", a);
        CHECK(cplus_nextdown(a) <= cplus_nextdown(b), "nextdown monotonic", a);
    }

    /* Round trips away from the boundaries. */
    CHECK(cplus_nextdown(cplus_nextup(1.0)) == 1.0, "nextdown(nextup(1.0))", 1.0);
    CHECK(cplus_nextup(cplus_nextdown(1.0)) == 1.0, "nextup(nextdown(1.0))", 1.0);
}

/* -------------------------------- float ---------------------------------- */
static void test_float(void) {
    const float inf = INFINITY;
    const float nan = NAN;
    const float minsub = FLT_SUBNORM_MIN;
    const float maxsub = nextafterf(FLT_MIN, 0.0f);
    const float inputs[] = {
        -inf, -FLT_MAX, -2.0f, -1.0f, -minsub, -0.0f, 0.0f,
        minsub, maxsub, FLT_MIN, 1.0f, 2.0f, FLT_MAX, inf, nan
    };
    size_t i;
    float nan_up, nan_down;

    for (i = 0; i < sizeof inputs / sizeof inputs[0]; ++i) {
        float x = inputs[i];
        float up_off, up_on, dn_off, dn_on;
        float want_up = nextafterf(x, inf);
        float want_dn = nextafterf(x, -inf);

        cplus_libc_log_set_enabled(0);
        up_off = cplus_nextupf(x);
        dn_off = cplus_nextdownf(x);
        cplus_libc_log_set_enabled(1);
        up_on = cplus_nextupf(x);
        dn_on = cplus_nextdownf(x);
        cplus_libc_log_set_enabled(0);

        CHECK(bits_equal_f(up_off, up_on), "nextupf logging parity", x);
        CHECK(bits_equal_f(dn_off, dn_on), "nextdownf logging parity", x);
        CHECK(bits_equal_f(up_on, want_up), "nextupf host differential", x);
        CHECK(bits_equal_f(dn_on, want_dn), "nextdownf host differential", x);

        if (!isnan(x)) {
            CHECK(up_on >= x, "nextupf direction (>= x)", x);
            CHECK(dn_on <= x, "nextdownf direction (<= x)", x);
            if (isfinite(x)) {
                CHECK(up_on > x, "nextupf strictly greater (finite)", x);
                CHECK(dn_on < x, "nextdownf strictly less (finite)", x);
            }
        }
    }

    CHECK(bits_equal_f(cplus_nextupf(0.0f), minsub), "nextupf(+0) -> +minsub", 0.0f);
    CHECK(bits_equal_f(cplus_nextupf(-0.0f), minsub), "nextupf(-0) -> +minsub", -0.0f);
    CHECK(bits_equal_f(cplus_nextdownf(0.0f), -minsub), "nextdownf(+0) -> -minsub", 0.0f);
    CHECK(bits_equal_f(cplus_nextdownf(-0.0f), -minsub), "nextdownf(-0) -> -minsub", -0.0f);
    CHECK(cplus_nextupf(FLT_MAX) == inf, "nextupf(FLT_MAX) -> +inf", FLT_MAX);
    CHECK(cplus_nextdownf(-FLT_MAX) == -inf, "nextdownf(-FLT_MAX) -> -inf", -FLT_MAX);
    CHECK(cplus_nextupf(inf) == inf, "nextupf(+inf) -> +inf", inf);
    CHECK(cplus_nextdownf(-inf) == -inf, "nextdownf(-inf) -> -inf", -inf);
    CHECK(cplus_nextupf(-inf) == -FLT_MAX, "nextupf(-inf) -> -FLT_MAX", -inf);
    CHECK(cplus_nextdownf(inf) == FLT_MAX, "nextdownf(+inf) -> FLT_MAX", inf);
    CHECK(cplus_nextupf(maxsub) == FLT_MIN, "nextupf(max subnormal) -> FLT_MIN", maxsub);
    CHECK(cplus_nextdownf(FLT_MIN) == maxsub, "nextdownf(FLT_MIN) -> max subnormal", FLT_MIN);
    CHECK(cplus_nextdownf(minsub) == 0.0f, "nextdownf(minsub) -> +0", minsub);
    CHECK(cplus_nextupf(1.0f) == 1.0f + FLT_EPSILON, "nextupf(1.0) anchor", 1.0f);

    nan_up = cplus_nextupf(nan);
    nan_down = cplus_nextdownf(nan);
    CHECK(isnan(nan_up), "nextupf(NAN) is NaN", nan);
    CHECK(isnan(nan_down), "nextdownf(NAN) is NaN", nan);
    CHECK(bits_equal_f(nan_up, nan), "nextupf(NAN) preserves bits", nan);
    CHECK(bits_equal_f(nan_down, nan), "nextdownf(NAN) preserves bits", nan);

    for (i = 0; i + 1 < sizeof inputs / sizeof inputs[0]; ++i) {
        float a = inputs[i], b = inputs[i + 1];
        if (isnan(a) || isnan(b)) continue;
        CHECK(cplus_nextupf(a) <= cplus_nextupf(b), "nextupf monotonic", a);
        CHECK(cplus_nextdownf(a) <= cplus_nextdownf(b), "nextdownf monotonic", a);
    }

    CHECK(cplus_nextdownf(cplus_nextupf(1.0f)) == 1.0f, "nextdownf(nextupf(1.0f))", 1.0f);
    CHECK(cplus_nextupf(cplus_nextdownf(1.0f)) == 1.0f, "nextupf(nextdownf(1.0f))", 1.0f);
}

/* ----------------------------- long double ------------------------------- */
/* long double is 16 bytes on this host with padding, so use == / isnan and
 * signbit rather than memcmp for its comparisons. */
static void test_ldouble(void) {
    const long double inf = INFINITY;
    const long double nan = NAN;
    const long double minsub = LDBL_SUBNORM_MIN;
    const long double maxsub = nextafterl(LDBL_MIN, 0.0L);
    const long double inputs[] = {
        -inf, -LDBL_MAX, -2.0L, -1.0L, -minsub, -0.0L, 0.0L,
        minsub, maxsub, LDBL_MIN, 1.0L, 2.0L, LDBL_MAX, inf, nan
    };
    size_t i;
    long double nan_up, nan_down;

    for (i = 0; i < sizeof inputs / sizeof inputs[0]; ++i) {
        long double x = inputs[i];
        long double up_off, up_on, dn_off, dn_on;
        long double want_up = nextafterl(x, inf);
        long double want_dn = nextafterl(x, -inf);

        cplus_libc_log_set_enabled(0);
        up_off = cplus_nextupl(x);
        dn_off = cplus_nextdownl(x);
        cplus_libc_log_set_enabled(1);
        up_on = cplus_nextupl(x);
        dn_on = cplus_nextdownl(x);
        cplus_libc_log_set_enabled(0);

        if (isnan(x)) {
            /* NaN cannot be compared with ==: use classification + sign. */
            CHECK(isnan(up_off) && isnan(up_on) &&
                  signbit(up_off) == signbit(up_on), "nextupl logging parity (NaN)", x);
            CHECK(isnan(dn_off) && isnan(dn_on) &&
                  signbit(dn_off) == signbit(dn_on), "nextdownl logging parity (NaN)", x);
            CHECK(isnan(up_on) && isnan(want_up) &&
                  signbit(up_on) == signbit(want_up), "nextupl host differential (NaN)", x);
            CHECK(isnan(dn_on) && isnan(want_dn) &&
                  signbit(dn_on) == signbit(want_dn), "nextdownl host differential (NaN)", x);
            continue;
        }

        CHECK(up_off == up_on, "nextupl logging parity", x);
        CHECK(dn_off == dn_on, "nextdownl logging parity", x);
        CHECK(up_on == want_up, "nextupl host differential", x);
        CHECK(dn_on == want_dn, "nextdownl host differential", x);

        CHECK(up_on >= x, "nextupl direction (>= x)", x);
        CHECK(dn_on <= x, "nextdownl direction (<= x)", x);
        if (isfinite(x)) {
            CHECK(up_on > x, "nextupl strictly greater (finite)", x);
            CHECK(dn_on < x, "nextdownl strictly less (finite)", x);
        }
    }

    CHECK(cplus_nextupl(0.0L) == minsub, "nextupl(+0) -> +minsub", 0.0L);
    CHECK(cplus_nextupl(-0.0L) == minsub, "nextupl(-0) -> +minsub", -0.0L);
    CHECK(cplus_nextdownl(0.0L) == -minsub, "nextdownl(+0) -> -minsub", 0.0L);
    CHECK(cplus_nextdownl(-0.0L) == -minsub, "nextdownl(-0) -> -minsub", -0.0L);
    CHECK(cplus_nextupl(LDBL_MAX) == inf, "nextupl(LDBL_MAX) -> +inf", LDBL_MAX);
    CHECK(cplus_nextdownl(-LDBL_MAX) == -inf, "nextdownl(-LDBL_MAX) -> -inf", -LDBL_MAX);
    CHECK(cplus_nextupl(inf) == inf, "nextupl(+inf) -> +inf", inf);
    CHECK(cplus_nextdownl(-inf) == -inf, "nextdownl(-inf) -> -inf", -inf);
    CHECK(cplus_nextupl(-inf) == -LDBL_MAX, "nextupl(-inf) -> -LDBL_MAX", -inf);
    CHECK(cplus_nextdownl(inf) == LDBL_MAX, "nextdownl(+inf) -> LDBL_MAX", inf);
    CHECK(cplus_nextupl(maxsub) == LDBL_MIN, "nextupl(max subnormal) -> LDBL_MIN", maxsub);
    CHECK(cplus_nextdownl(LDBL_MIN) == maxsub, "nextdownl(LDBL_MIN) -> max subnormal", LDBL_MIN);
    CHECK(cplus_nextdownl(minsub) == 0.0L, "nextdownl(minsub) -> +0", minsub);
    CHECK(cplus_nextupl(1.0L) == 1.0L + LDBL_EPSILON, "nextupl(1.0) anchor", 1.0L);

    nan_up = cplus_nextupl(nan);
    nan_down = cplus_nextdownl(nan);
    CHECK(isnan(nan_up), "nextupl(NAN) is NaN", nan);
    CHECK(isnan(nan_down), "nextdownl(NAN) is NaN", nan);
    CHECK(signbit(nan_up) == signbit(nan), "nextupl(NAN) preserves sign", nan);
    CHECK(signbit(nan_down) == signbit(nan), "nextdownl(NAN) preserves sign", nan);

    for (i = 0; i + 1 < sizeof inputs / sizeof inputs[0]; ++i) {
        long double a = inputs[i], b = inputs[i + 1];
        if (isnan(a) || isnan(b)) continue;
        CHECK(cplus_nextupl(a) <= cplus_nextupl(b), "nextupl monotonic", a);
        CHECK(cplus_nextdownl(a) <= cplus_nextdownl(b), "nextdownl monotonic", a);
    }

    CHECK(cplus_nextdownl(cplus_nextupl(1.0L)) == 1.0L, "nextdownl(nextupl(1.0L))", 1.0L);
    CHECK(cplus_nextupl(cplus_nextdownl(1.0L)) == 1.0L, "nextupl(nextdownl(1.0L))", 1.0L);
}

int main(void) {
    cplus_libc_log_set_enabled(0); /* opt-in logging is OFF by default */

    test_double();
    test_float();
    test_ldouble();

    printf("test_math_c23_next: checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
