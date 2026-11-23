/*
 * test_c23_arith.c -- standalone deterministic C99 tests for the 12 C23
 * fadd/fsub/fmul/fdiv entry points implemented in libc/src/math/c23_arith.c.
 *
 * Coverage:
 *   (1) representative finite values for all 12 entry points;
 *   (2) IEEE 754 signed-zero results;
 *   (3) infinities (inf - inf -> NaN, finite/0 -> inf, 0/0 -> NaN);
 *   (4) NaN propagation;
 *   (5) rounding-mode sensitivity under all four host modes where available;
 *   (6) standard FP exception flags inherited from the host operators;
 *   (7) logging ON/OFF result parity.
 *
 * Output: one line "c23_arith: <checks> <failures>" on stdout; exit status is
 * nonzero iff failures > 0. Exact build/run commands and captured output are
 * recorded in docs/libc/C23_ARITH.md.
 */
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cplus_c23_arith.h"

extern void cplus_libc_log_set_enabled(int);

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, (msg));            \
        }                                                                      \
    } while (0)

static uint32_t bits_f(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

/* (1) representative finite values ---------------------------------------- */
static void test_representative(void) {
    CHECK(cplus_fadd(1.5, 2.25) == 3.75f, "fadd representative");
    CHECK(cplus_faddf(1.5f, 2.25f) == 3.75f, "faddf representative");
    CHECK((double)cplus_faddl(1.5L, 2.25L) == 3.75, "faddl representative");

    CHECK(cplus_fsub(5.0, 2.5) == 2.5f, "fsub representative");
    CHECK(cplus_fsubf(5.0f, 2.5f) == 2.5f, "fsubf representative");
    CHECK((double)cplus_fsubl(5.0L, 2.5L) == 2.5, "fsubl representative");

    CHECK(cplus_fmul(2.0, 3.0) == 6.0f, "fmul representative");
    CHECK(cplus_fmulf(2.0f, 3.0f) == 6.0f, "fmulf representative");
    CHECK((double)cplus_fmull(2.0L, 3.0L) == 6.0, "fmull representative");

    CHECK(cplus_fdiv(7.0, 2.0) == 3.5f, "fdiv representative");
    CHECK(cplus_fdivf(7.0f, 2.0f) == 3.5f, "fdivf representative");
    CHECK((double)cplus_fdivl(7.0L, 2.0L) == 3.5, "fdivl representative");
}

/* (2) signed zero --------------------------------------------------------- */
static void test_signed_zero(void) {
    CHECK(signbit(cplus_fadd(-0.0, -0.0)), "fadd(-0,-0) -> -0");
    CHECK(!signbit(cplus_fadd(0.0, -0.0)), "fadd(+0,-0) -> +0");
    CHECK(signbit(cplus_faddf(-0.0f, -0.0f)), "faddf(-0,-0) -> -0");
    CHECK(signbit(cplus_faddl(-0.0L, -0.0L)), "faddl(-0,-0) -> -0");

    CHECK(!signbit(cplus_fsub(0.0, 0.0)), "fsub(+0,+0) -> +0");
    CHECK(signbit(cplus_fsub(-0.0, 0.0)), "fsub(-0,+0) -> -0");
    CHECK(signbit(cplus_fsubf(-0.0f, 0.0f)), "fsubf(-0,+0) -> -0");
    CHECK(signbit(cplus_fsubl(-0.0L, 0.0L)), "fsubl(-0,+0) -> -0");

    CHECK(signbit(cplus_fmul(-1.0, 0.0)), "fmul(-1,+0) -> -0");
    CHECK(signbit(cplus_fmulf(-1.0f, 0.0f)), "fmulf(-1,+0) -> -0");
    CHECK(signbit(cplus_fmull(-1.0L, 0.0L)), "fmull(-1,+0) -> -0");

    CHECK(signbit(cplus_fdiv(-1.0, INFINITY)), "fdiv(-1,+inf) -> -0");
    CHECK(signbit(cplus_fdivf(-1.0f, INFINITY)), "fdivf(-1,+inf) -> -0");
    CHECK(signbit(cplus_fdivl(-1.0L, INFINITY)), "fdivl(-1,+inf) -> -0");
}

/* (3) infinities ---------------------------------------------------------- */
static void test_infinities(void) {
    CHECK(isinf(cplus_fadd(INFINITY, 1.0)), "fadd(+inf,1) -> +inf");
    CHECK(isnan(cplus_fadd(INFINITY, -INFINITY)), "fadd(+inf,-inf) -> NaN");
    CHECK(isinf(cplus_faddf(-INFINITY, -1.0f)), "faddf(-inf,-1) -> -inf");
    CHECK(isinf(cplus_faddl(INFINITY, 1.0L)), "faddl(+inf,1) -> +inf");

    CHECK(isinf(cplus_fsub(INFINITY, -INFINITY)), "fsub(+inf,-inf) -> +inf");
    CHECK(isnan(cplus_fsubf(INFINITY, INFINITY)), "fsubf(+inf,+inf) -> NaN");
    CHECK(isinf(cplus_fsubl(-INFINITY, 1.0L)), "fsubl(-inf,1) -> -inf");

    CHECK(isnan(cplus_fmul(0.0, INFINITY)), "fmul(0,+inf) -> NaN");
    CHECK(isinf(cplus_fmulf(-2.0f, INFINITY)), "fmulf(-2,+inf) -> -inf");
    CHECK(isinf(cplus_fmull(2.0L, INFINITY)), "fmull(2,+inf) -> +inf");

    CHECK(isinf(cplus_fdiv(1.0, 0.0)), "fdiv(1,+0) -> +inf");
    CHECK(isinf(cplus_fdivf(-1.0f, 0.0f)), "fdivf(-1,+0) -> -inf");
    CHECK(isnan(cplus_fdivl(0.0L, 0.0L)), "fdivl(0,0) -> NaN");
}

/* (4) NaN propagation ----------------------------------------------------- */
static void test_nan(void) {
    CHECK(isnan(cplus_fadd(NAN, 1.0)), "fadd(NaN,1) -> NaN");
    CHECK(isnan(cplus_fsubf(1.0f, NAN)), "fsubf(1,NaN) -> NaN");
    CHECK(isnan(cplus_fmulf(NAN, 0.0f)), "fmulf(NaN,0) -> NaN");
    CHECK(isnan(cplus_fdivl(1.0L, NAN)), "fdivl(1,NaN) -> NaN");
}

/* (5) rounding-mode sensitivity ------------------------------------------- */
static void test_rounding_modes(void) {
    /* 1.0 + 2^-24 is exactly the halfway point between 1.0 and the next
     * binary32 value above it (spacing 2^-23). Ties-to-even and downward/
     * towardzero keep 1.0; upward rounds up by one ULP. */
    const uint32_t kTies = 0x3f800000u; /* 1.0f */
    const uint32_t kUp = 0x3f800001u;   /* nextafterf(1.0f, +inf) */
    const double half_ulp = 0x1p-24;
    const float half_ulp_f = 0x1p-24f;
    const long double half_ulp_l = 0x1p-24L;
    int original = fegetround();
    int i;

    static const int kModes[] = {FE_TONEAREST, FE_UPWARD, FE_DOWNWARD,
                                 FE_TOWARDZERO};

    for (i = 0; i < 4; ++i) {
        if (fesetround(kModes[i]) != 0) {
            CHECK(0, "fesetround unavailable");
            continue;
        }
        uint32_t want_add = (kModes[i] == FE_UPWARD) ? kUp : kTies;
        uint32_t want_sub = (kModes[i] == FE_DOWNWARD ||
                             kModes[i] == FE_TOWARDZERO)
                                ? 0x3f7fffffu
                                : kTies;
        CHECK(bits_f(cplus_fadd(1.0, half_ulp)) == want_add,
              "fadd rounding mode");
        CHECK(bits_f(cplus_faddf(1.0f, half_ulp_f)) == want_add,
              "faddf rounding mode");
        CHECK(bits_f(cplus_faddl(1.0L, half_ulp_l)) == want_add,
              "faddl rounding mode");
        /* 1.0 - 2^-25 is the halfway point between 1.0 and the binary32
         * value just below it (0x3f7fffff). */
        CHECK(bits_f(cplus_fsub(1.0, 0x1p-25)) == want_sub,
              "fsub rounding mode");
        CHECK(bits_f(cplus_fsubf(1.0f, 0x1p-25f)) == want_sub,
              "fsubf rounding mode");
        CHECK(bits_f(cplus_fsubl(1.0L, 0x1p-25L)) == want_sub,
              "fsubl rounding mode");
    }
    (void)fesetround(original);
}

/* (6) standard FP exception flags ----------------------------------------- */
static void test_exceptions(void) {
    volatile double dinf = INFINITY;
    volatile double dmax = DBL_MAX;
    volatile float fmax = FLT_MAX;
    volatile double dz = 0.0;
    volatile double r1;
    volatile float r2;

    feclearexcept(FE_ALL_EXCEPT);
    r1 = cplus_fadd(dinf, -dinf);
    (void)r1;
    CHECK(fetestexcept(FE_INVALID) != 0, "fadd raises FE_INVALID");

    feclearexcept(FE_ALL_EXCEPT);
    r1 = cplus_fdiv(1.0, dz);
    (void)r1;
    CHECK(fetestexcept(FE_DIVBYZERO) != 0, "fdiv raises FE_DIVBYZERO");

    feclearexcept(FE_ALL_EXCEPT);
    r1 = cplus_fadd(dmax, dmax);
    (void)r1;
    CHECK(fetestexcept(FE_OVERFLOW) != 0, "fadd raises FE_OVERFLOW");

    feclearexcept(FE_ALL_EXCEPT);
    r2 = cplus_fmulf(fmax, fmax);
    (void)r2;
    CHECK(fetestexcept(FE_OVERFLOW) != 0, "fmulf raises FE_OVERFLOW");

    feclearexcept(FE_ALL_EXCEPT);
}

/* (7) logging ON/OFF parity ------------------------------------------------ */
static void test_logging_parity(void) {
    cplus_libc_log_set_enabled(0);
    uint32_t off[12];
    off[0] = bits_f(cplus_fadd(1.0, 0x1p-24));
    off[1] = bits_f(cplus_faddf(1.0f, 0x1p-24f));
    off[2] = bits_f(cplus_faddl(1.0L, 0x1p-24L));
    off[3] = bits_f(cplus_fsub(1.0, 0x1p-25));
    off[4] = bits_f(cplus_fsubf(1.0f, 0x1p-25f));
    off[5] = bits_f(cplus_fsubl(1.0L, 0x1p-25L));
    off[6] = bits_f(cplus_fmul(3.0, 5.0));
    off[7] = bits_f(cplus_fmulf(3.0f, 5.0f));
    off[8] = bits_f(cplus_fmull(3.0L, 5.0L));
    off[9] = bits_f(cplus_fdiv(3.0, 5.0));
    off[10] = bits_f(cplus_fdivf(3.0f, 5.0f));
    off[11] = bits_f(cplus_fdivl(3.0L, 5.0L));

    cplus_libc_log_set_enabled(1);
    uint32_t on[12];
    on[0] = bits_f(cplus_fadd(1.0, 0x1p-24));
    on[1] = bits_f(cplus_faddf(1.0f, 0x1p-24f));
    on[2] = bits_f(cplus_faddl(1.0L, 0x1p-24L));
    on[3] = bits_f(cplus_fsub(1.0, 0x1p-25));
    on[4] = bits_f(cplus_fsubf(1.0f, 0x1p-25f));
    on[5] = bits_f(cplus_fsubl(1.0L, 0x1p-25L));
    on[6] = bits_f(cplus_fmul(3.0, 5.0));
    on[7] = bits_f(cplus_fmulf(3.0f, 5.0f));
    on[8] = bits_f(cplus_fmull(3.0L, 5.0L));
    on[9] = bits_f(cplus_fdiv(3.0, 5.0));
    on[10] = bits_f(cplus_fdivf(3.0f, 5.0f));
    on[11] = bits_f(cplus_fdivl(3.0L, 5.0L));

    for (int i = 0; i < 12; ++i) {
        CHECK(off[i] == on[i], "logging parity");
    }
    cplus_libc_log_set_enabled(0);
}

int main(void) {
#if CPLUS_C23_ARITH_HAVE_HOST_OPERATORS
    test_representative();
    test_signed_zero();
    test_infinities();
    test_nan();
    test_rounding_modes();
    test_exceptions();
    test_logging_parity();
#else
    CHECK(0, "host operator backend unavailable");
#endif
    printf("c23_arith: %d %d\n", g_checks, g_failures);
    return g_failures != 0;
}
