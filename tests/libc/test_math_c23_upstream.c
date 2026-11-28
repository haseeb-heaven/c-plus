/*
 * test_math_c23_upstream.c -- standalone C99 tests for the C23 <math.h> APIs
 * audited for copy-from-upstream in libc/src/math/math_c23_upstream.c.
 *
 * AUDIT RESULT (2026-11-23): 0 of 23 APIs could be copied from LLVM libc at
 * tag llvmorg-20.1.1. Every upstream implementation delegates into LLVM libc
 * C++ internals (src/__support/FPUtil/...), which the integration rule forbids
 * importing, so math_c23_upstream.c emits no definitions. Accordingly every
 * entry point below is SKIPPED WITH AN EXACT REASON rather than asserting a
 * fabricated or emulated result. See docs/libc/C23_MATH_UPSTREAM.md.
 *
 * The differential/classification/logging-parity helpers are kept (guarded by
 * CPLUS_C23_UPSTREAM_ANY_ENABLED) so the moment an upstream dependency closure
 * becomes self-contained and a body is enabled in the source, the same tests
 * can be switched on without rewriting the harness.
 */

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

extern void cplus_libc_log_set_enabled(int);

/* Set to 1 only for an API whose upstream body was actually copied. The audit
 * found none, so every gate stays 0 and the helpers below compile out. */
#define CPLUS_C23_UPSTREAM_ANY_ENABLED 0

static int g_tested = 0;
static int g_skipped = 0;

static void note_skip(const char *api, const char *why) {
    printf("math_c23_upstream: SKIP %-12s (%s)\n", api, why);
    ++g_skipped;
}

#if CPLUS_C23_UPSTREAM_ANY_ENABLED
/* --- reusable oracle helpers (dormant until a copy is enabled) ---------- */
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
/* Each future copied API would add a test that:
 *   1) compares bit-exactly (with tolerance + classification fallback)
 *      against a host differential oracle,
 *   2) checks NaN/Inf/signed-zero classification, and
 *   3) verifies logging OFF/ON parity via cplus_libc_log_set_enabled.
 */
#endif /* CPLUS_C23_UPSTREAM_ANY_ENABLED */

static void report_blocked_apis(void) {
    struct {
        const char *api;
        const char *why;
    } blocked[] = {
        {"nextup",  "upstream nextup.cpp -> fputil::nextupdown (src/__support/FPUtil/ManipulationFunctions.h)"},
        {"nextupf", "upstream nextupf.cpp -> fputil::nextupdown (src/__support C++ template)"},
        {"nextupl", "upstream nextupl.cpp -> fputil::nextupdown (src/__support C++ template)"},
        {"nextdown",  "upstream nextdown.cpp -> fputil::nextupdown (src/__support C++ template)"},
        {"nextdownf", "upstream nextdownf.cpp -> fputil::nextupdown (src/__support C++ template)"},
        {"nextdownl", "upstream nextdownl.cpp -> fputil::nextupdown (src/__support C++ template)"},
        {"fadd",  "upstream add_sub.h (DyadicFloat/FPBits) + signature `float fadd(double,double)` != C23"},
        {"faddf", "no upstream faddf.cpp at this tag (HTTP 404) and same __support blocker"},
        {"faddl", "upstream add_sub.h + signature `float faddl(long double,long double)` != C23"},
        {"fsub",  "upstream add_sub.h (DyadicFloat/FPBits) + signature mismatch != C23"},
        {"fsubf", "no upstream fsubf.cpp at this tag (HTTP 404) and same __support blocker"},
        {"fsubl", "upstream add_sub.h + signature mismatch != C23"},
        {"fmul",  "upstream mul.h + double_double.h (exact_mult/DoubleDouble) + signature mismatch"},
        {"fmulf", "no upstream fmulf.cpp at this tag (HTTP 404) and same __support blocker"},
        {"fmull", "upstream mul.h (DyadicFloat/quick_mul) + signature mismatch != C23"},
        {"fdiv",  "upstream div.h (DyadicFloat/FPBits) + signature mismatch != C23"},
        {"fdivf", "no upstream fdivf.cpp at this tag (HTTP 404) and same __support blocker"},
        {"fdivl", "upstream div.h (DyadicFloat/FPBits) + signature mismatch != C23"},
        {"hypot3", "no 3-arg hypot upstream at this tag (hypot.cpp is 2-arg via fputil::hypot)"},
        {"frexp16", "upstream frexpf16.cpp -> fputil::frexp (src/__support FPBits/NormalFloat)"},
        {"fma16",   "no upstream fmaf16.cpp; f16fma.cpp -> fputil::fma<float16> + signature mismatch"},
        {"logb16",  "upstream logbf16.cpp -> fputil::logb (src/__support FPBits/DyadicFloat)"},
        {"exp16",   "upstream expf16.cpp uses fputil::FPBits/ExceptValues/polyeval + expxf16.h tables"},
    };
    size_t i;
    for (i = 0; i < sizeof blocked / sizeof blocked[0]; ++i)
        note_skip(blocked[i].api, blocked[i].why);
}

int main(void) {
    /* Harness sanity: the logging toggle used by the dormant parity tests is
     * linkable and callable. This checks integration, not any C23 API result. */
    cplus_libc_log_set_enabled(0);
    cplus_libc_log_set_enabled(1);
    cplus_libc_log_set_enabled(0);
    ++g_tested;

    report_blocked_apis();

    printf("math_c23_upstream: harness ok; APIs tested=%d, skipped=%d\n",
           g_tested - 1, g_skipped);
    printf("math_c23_upstream: ok\n");
    return 0;
}
