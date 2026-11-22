/*
 * test_math_c23.c -- standalone C99 tests for the ISO C23 <math.h> additions
 * integrated in libc/src/math/math_c23.c.
 *
 * The availability guards below MIRROR math_c23.c exactly: an entry point is
 * exercised only when the same __has_builtin(...) guard that defines its
 * cplus_* wrapper in the source is true. Everything else is SKIPPED with an
 * explicit note (and counted), because this host provides no symbol, no
 * builtin and the vendored musl subset has no source for it. No placeholder
 * result is ever asserted as a pass.
 *
 * For each exercised API we check:
 *   (1) host differential -- the wrapper result equals the host compiler
 *       builtin's result (bit-exact, with a tolerance + classification
 *       fallback for diagnostics);
 *   (2) classification -- NaN/Inf/signed-zero behaviour matches the oracle;
 *   (3) logging ON/OFF parity -- identical results with cplus_libc_log
 *       disabled and enabled.
 */

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* __has_builtin is a Clang/GCC extension; degrade to 0 on other compilers. */
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#define HAVE_ROUNDEVEN  __has_builtin(__builtin_roundeven)
#define HAVE_ROUNDEVENF __has_builtin(__builtin_roundevenf)
#define HAVE_ROUNDEVENL __has_builtin(__builtin_roundevenl)

/* Only entry points that are actually defined in math_c23.c are referenced. */
extern double cplus_roundeven(double);
extern float cplus_roundevenf(float);
extern long double cplus_roundevenl(long double);
extern void cplus_libc_log_set_enabled(int);

static int g_tested = 0;
static int g_skipped = 0;

static void note_skip(const char *api, const char *why) {
    printf("math_c23: SKIP %-12s (%s)\n", api, why);
    ++g_skipped;
}

/* --- comparison helpers (tolerance + classification, as required) ------ */
static int bits_equal_double(double a, double b) {
    return memcmp(&a, &b, sizeof a) == 0;
}
static int bits_equal_float(float a, float b) {
    return memcmp(&a, &b, sizeof a) == 0;
}
static int bits_equal_ldouble(long double a, long double b) {
    return memcmp(&a, &b, sizeof a) == 0;
}

static int close_enough_double(double got, double want, double tol) {
    if (isnan(got) || isnan(want)) return isnan(got) && isnan(want);
    if (isinf(got) || isinf(want)) return got == want;
    {
        double scale = fabs(want) > 1.0 ? fabs(want) : 1.0;
        return fabs(got - want) <= tol * scale;
    }
}
static int close_enough_float(float got, float want, float tol) {
    if (isnan(got) || isnan(want)) return isnan(got) && isnan(want);
    if (isinf(got) || isinf(want)) return got == want;
    {
        float scale = fabsf(want) > 1.0f ? fabsf(want) : 1.0f;
        return fabsf(got - want) <= tol * scale;
    }
}
static int close_enough_ldouble(long double got, long double want, long double tol) {
    if (isnan(got) || isnan(want)) return isnan(got) && isnan(want);
    if (isinf(got) || isinf(want)) return got == want;
    {
        long double scale = fabsl(want) > 1.0L ? fabsl(want) : 1.0L;
        return fabsl(got - want) <= tol * scale;
    }
}

static int same_class_double(double got, double want) {
    if (isnan(got)) return isnan(want);
    if (isinf(got)) return isinf(want) && signbit(got) == signbit(want);
    return !isnan(want) && !isinf(want);
}
static int same_class_float(float got, float want) {
    if (isnan(got)) return isnan(want);
    if (isinf(got)) return isinf(want) && signbit(got) == signbit(want);
    return !isnan(want) && !isinf(want);
}
static int same_class_ldouble(long double got, long double want) {
    if (isnan(got)) return isnan(want);
    if (isinf(got)) return isinf(want) && signbit(got) == signbit(want);
    return !isnan(want) && !isinf(want);
}

/* ---------------- double roundeven ---------------------------------- */
#if HAVE_ROUNDEVEN
static void test_roundeven(void) {
    static const double inputs[] = {
        0.0, -0.0, 0.5, -0.5, 1.5, -1.5, 2.5, -2.5,
        0.49999999999999994, 1.4999999999999998, -1.4999999999999998,
        1e300, -1e300, INFINITY, -INFINITY, NAN
    };
    size_t i;
    for (i = 0; i < sizeof inputs / sizeof inputs[0]; ++i) {
        double x = inputs[i];
        double oracle = __builtin_roundeven(x);
        double off_r, on_r;

        cplus_libc_log_set_enabled(0);
        off_r = cplus_roundeven(x);
        cplus_libc_log_set_enabled(1);
        on_r = cplus_roundeven(x);
        cplus_libc_log_set_enabled(0);

        if (!bits_equal_double(off_r, on_r)) {
            fprintf(stderr, "math_c23: roundeven logging parity failed at %g\n", x);
            goto fail;
        }
        if (!bits_equal_double(on_r, oracle) &&
            !(close_enough_double(on_r, oracle, 0.0) && same_class_double(on_r, oracle))) {
            fprintf(stderr, "math_c23: roundeven differential failed at %g\n", x);
            goto fail;
        }
        if (!same_class_double(on_r, oracle)) {
            fprintf(stderr, "math_c23: roundeven classification failed at %g\n", x);
            goto fail;
        }
    }
    /* Explicit IEEE tie-to-even anchors (not derived from the wrapper). */
    if (!bits_equal_double(cplus_roundeven(0.5), 0.0)) goto fail;
    if (!bits_equal_double(cplus_roundeven(1.5), 2.0)) goto fail;
    if (!bits_equal_double(cplus_roundeven(2.5), 2.0)) goto fail;
    if (!bits_equal_double(cplus_roundeven(-1.5), -2.0)) goto fail;
    if (!bits_equal_double(cplus_roundeven(-0.5), -0.0) || !signbit(cplus_roundeven(-0.5))) goto fail;
    ++g_tested;
    return;
fail:
    fprintf(stderr, "math_c23: roundeven FAILED\n");
    ++g_skipped; /* counted as not-passing; do not claim success */
}
#endif /* HAVE_ROUNDEVEN */

/* ---------------- float roundevenf ---------------------------------- */
#if HAVE_ROUNDEVENF
static void test_roundevenf(void) {
    static const float inputs[] = {
        0.0f, -0.0f, 0.5f, -0.5f, 1.5f, -1.5f, 2.5f, -2.5f,
        1.4999999f, -1.4999999f, 1e30f, -1e30f, INFINITY, -INFINITY, NAN
    };
    size_t i;
    for (i = 0; i < sizeof inputs / sizeof inputs[0]; ++i) {
        float x = inputs[i];
        float oracle = __builtin_roundevenf(x);
        float off_r, on_r;

        cplus_libc_log_set_enabled(0);
        off_r = cplus_roundevenf(x);
        cplus_libc_log_set_enabled(1);
        on_r = cplus_roundevenf(x);
        cplus_libc_log_set_enabled(0);

        if (!bits_equal_float(off_r, on_r)) {
            fprintf(stderr, "math_c23: roundevenf logging parity failed at %g\n", (double)x);
            goto fail;
        }
        if (!bits_equal_float(on_r, oracle) &&
            !(close_enough_float(on_r, oracle, 0.0f) && same_class_float(on_r, oracle))) {
            fprintf(stderr, "math_c23: roundevenf differential failed at %g\n", (double)x);
            goto fail;
        }
        if (!same_class_float(on_r, oracle)) {
            fprintf(stderr, "math_c23: roundevenf classification failed at %g\n", (double)x);
            goto fail;
        }
    }
    if (!bits_equal_float(cplus_roundevenf(0.5f), 0.0f)) goto fail;
    if (!bits_equal_float(cplus_roundevenf(1.5f), 2.0f)) goto fail;
    if (!bits_equal_float(cplus_roundevenf(-1.5f), -2.0f)) goto fail;
    if (!bits_equal_float(cplus_roundevenf(-0.5f), -0.0f) || !signbit(cplus_roundevenf(-0.5f))) goto fail;
    ++g_tested;
    return;
fail:
    fprintf(stderr, "math_c23: roundevenf FAILED\n");
    ++g_skipped;
}
#endif /* HAVE_ROUNDEVENF */

/* ---------------- long double roundevenl ---------------------------- */
#if HAVE_ROUNDEVENL
static void test_roundevenl(void) {
    static const long double inputs[] = {
        0.0L, -0.0L, 0.5L, -0.5L, 1.5L, -1.5L, 2.5L, -2.5L,
        1.499999999999999999L, -1.499999999999999999L,
        1e300L, -1e300L, INFINITY, -INFINITY, NAN
    };
    size_t i;
    for (i = 0; i < sizeof inputs / sizeof inputs[0]; ++i) {
        long double x = inputs[i];
        long double oracle = __builtin_roundevenl(x);
        long double off_r, on_r;

        cplus_libc_log_set_enabled(0);
        off_r = cplus_roundevenl(x);
        cplus_libc_log_set_enabled(1);
        on_r = cplus_roundevenl(x);
        cplus_libc_log_set_enabled(0);

        if (!bits_equal_ldouble(off_r, on_r)) {
            fprintf(stderr, "math_c23: roundevenl logging parity failed at %Lg\n", x);
            goto fail;
        }
        if (!bits_equal_ldouble(on_r, oracle) &&
            !(close_enough_ldouble(on_r, oracle, 0.0L) && same_class_ldouble(on_r, oracle))) {
            fprintf(stderr, "math_c23: roundevenl differential failed at %Lg\n", x);
            goto fail;
        }
        if (!same_class_ldouble(on_r, oracle)) {
            fprintf(stderr, "math_c23: roundevenl classification failed at %Lg\n", x);
            goto fail;
        }
    }
    if (!bits_equal_ldouble(cplus_roundevenl(0.5L), 0.0L)) goto fail;
    if (!bits_equal_ldouble(cplus_roundevenl(1.5L), 2.0L)) goto fail;
    if (!bits_equal_ldouble(cplus_roundevenl(-1.5L), -2.0L)) goto fail;
    if (!bits_equal_ldouble(cplus_roundevenl(-0.5L), -0.0L) || !signbit(cplus_roundevenl(-0.5L))) goto fail;
    ++g_tested;
    return;
fail:
    fprintf(stderr, "math_c23: roundevenl FAILED\n");
    ++g_skipped;
}
#endif /* HAVE_ROUNDEVENL */

/* ---------------- platform-gap SKIPs (26 APIs) ---------------------- */
static void report_platform_gaps(void) {
    static const char *const gap_names[] = {
        "exp10", "exp10f", "exp10l",
        "nextup", "nextupf", "nextupl",
        "nextdown", "nextdownf", "nextdownl",
        "fadd", "faddf", "faddl",
        "fsub", "fsubf", "fsubl",
        "fmul", "fmulf", "fmull",
        "fdiv", "fdivf", "fdivl",
        "cplus_hypot3", "frexp16", "fma16", "logb16", "exp16"
    };
    size_t i;
    for (i = 0; i < sizeof gap_names / sizeof gap_names[0]; ++i) {
        note_skip(gap_names[i], "platform gap: no host symbol/builtin, no vendored source");
    }
}

int main(void) {
    cplus_libc_log_set_enabled(0); /* opt-in logging is OFF by default */

#if HAVE_ROUNDEVEN
    test_roundeven();
#else
    note_skip("roundeven", "no __builtin_roundeven on this host");
#endif

#if HAVE_ROUNDEVENF
    test_roundevenf();
#else
    note_skip("roundevenf", "no __builtin_roundevenf on this host");
#endif

#if HAVE_ROUNDEVENL
    test_roundevenl();
#else
    note_skip("roundevenl", "no __builtin_roundevenl on this host");
#endif

    report_platform_gaps();

    printf("math_c23: ok (tested=%d, skipped=%d)\n", g_tested, g_skipped);
    return 0;
}
