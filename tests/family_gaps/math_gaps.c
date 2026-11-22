/*
 * tests/family_gaps/math_gaps.c -- evidence harness for the 23 remaining
 * C23 <math.h> gaps recorded as MISSING in docs/libc/iso_c_inventory.json.
 *
 * Scope and honesty policy
 * ------------------------
 * None of the 23 APIs is implemented in this library, so this file does NOT
 * define or call them and does NOT substitute a fake result for them. Every
 * gap is reported with one exact `SKIP <api>: <reason>` line; the reason is the
 * upstream dependency (or missing upstream file) recorded in
 * docs/libc/C23_MATH_UPSTREAM.md, pinned at llvmorg-20.1.1.
 *
 * After each SKIP the harness still *exercises* the nearest available host /
 * C+ equivalent (nextafter for nextup/nextdown, ordinary operator arithmetic
 * for fadd/fsub/fmul/fdiv, nested hypot for 3-arg hypot, frexp/fma/logb/exp
 * for the _Float16 quartet) and compares it against the corresponding
 * cplus_* wrapper with a finite-value tolerance plus exact NaN/Inf/signed-zero
 * classification.  Those checks are real, counted as `tested`, and are NOT
 * claims that the C23 API itself works.
 *
 * One differential section then compares every linkable double, float and
 * long-double math wrapper in libc/install-include/cplus_libc.h against the
 * identically named host <math.h> function for NaN, +Inf, -Inf, +0, -0, a
 * per-function domain/negative input, binary special-input pairs, ternary
 * fma inputs, integer-exponent and integer-return families, out-parameter
 * families (frexp/modf/remquo), nexttoward, exp10 and roundeven.
 *
 * No files outside tests/family_gaps/math_gaps.c and
 * docs/libc/MATH_GAPS_EVIDENCE.md were created or modified by this task.
 */

#include "../../libc/install-include/cplus_libc.h"

#include <fenv.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef DBL_TRUE_MIN
#define DBL_TRUE_MIN (nextafter(0.0, 1.0))
#endif
#ifndef FLT_TRUE_MIN
#define FLT_TRUE_MIN (nextafterf(0.0f, 1.0f))
#endif
#ifndef LDBL_TRUE_MIN
#define LDBL_TRUE_MIN (nextafterl(0.0L, 1.0L))
#endif

static int g_tested = 0;
static int g_skipped = 0;
static int g_failed = 0;
static int g_wrappers = 0;

static void check(int ok, const char *what) {
    ++g_tested;
    if (!ok) {
        printf("FAIL %s\n", what);
        ++g_failed;
    }
}

static void note_skip(const char *api, const char *why) {
    printf("SKIP %s: %s\n", api, why);
    ++g_skipped;
}

/* --- comparison oracles ------------------------------------------------- */
/* NaN compares by class only (payload is unspecified); Inf and signed zero
 * compare exactly; finite values use a relative/absolute tolerance so that a
 * 1-ulp difference on a different host is not mistaken for a wrapper bug. */

static int same_double(double got, double want) {
    double scale;
    if (isnan(got) || isnan(want)) return isnan(got) && isnan(want);
    if (isinf(got) || isinf(want)) return got == want; /* includes sign */
    if (got == 0.0 || want == 0.0)
        return got == want && signbit(got) == signbit(want);
    scale = fabs(want) > 1.0 ? fabs(want) : 1.0;
    return fabs(got - want) <= 1e-12 * scale;
}

static int same_float(float got, float want) {
    float scale;
    if (isnan(got) || isnan(want)) return isnan(got) && isnan(want);
    if (isinf(got) || isinf(want)) return got == want;
    if (got == 0.0f || want == 0.0f)
        return got == want && signbit(got) == signbit(want);
    scale = fabsf(want) > 1.0f ? fabsf(want) : 1.0f;
    return fabsf(got - want) <= 1e-5f * scale;
}

static int same_ldouble(long double got, long double want) {
    long double scale;
    if (isnan(got) || isnan(want)) return isnan(got) && isnan(want);
    if (isinf(got) || isinf(want)) return got == want;
    if (got == 0.0L || want == 0.0L)
        return got == want && signbit(got) == signbit(want);
    scale = fabsl(want) > 1.0L ? fabsl(want) : 1.0L;
    return fabsl(got - want) <= 1e-12L * scale;
}

/* ======================================================================= */
/* 23 explicitly skipped C23 gaps + nearest-equivalent exercises           */
/* ======================================================================= */

static void exercise_gap_equivalents(void) {
    /* 1. nextup ---------------------------------------------------------- */
    note_skip("nextup",
              "no cplus_nextup; llvmorg-20.1.1 nextup.cpp -> "
              "fputil::nextupdown<false> in src/__support/FPUtil/"
              "ManipulationFunctions.h (C++ template dependency)");
    {
        double u = nextafter(1.0, INFINITY);
        check(same_double(cplus_nextafter(1.0, INFINITY), u) &&
                  same_double(u, 1.0 + DBL_EPSILON) && u > 1.0,
              "nextup ~ nextafter(x,+Inf) double");
        check(same_double(cplus_nextafter(0.0, INFINITY), DBL_TRUE_MIN) &&
                  same_double(cplus_nextafter(-0.0, INFINITY), DBL_TRUE_MIN),
              "nextup ~ signed-zero -> DBL_TRUE_MIN");
        check(isinf(cplus_nextafter(INFINITY, INFINITY)),
              "nextup ~ +Inf stays +Inf");
        puts("EQUIV nextup: nextafter(x,+Inf) exercised");
    }

    /* 2. nextupf --------------------------------------------------------- */
    note_skip("nextupf",
              "no cplus_nextupf; llvmorg-20.1.1 nextupf.cpp -> "
              "fputil::nextupdown<false> (src/__support C++ template)");
    {
        float u = nextafterf(1.0f, INFINITY);
        check(same_float(cplus_nextafterf(1.0f, INFINITY), u) &&
                  same_float(u, 1.0f + FLT_EPSILON) && u > 1.0f,
              "nextupf ~ nextafterf(x,+Inf)");
        check(same_float(cplus_nextafterf(0.0f, INFINITY), FLT_TRUE_MIN) &&
                  same_float(cplus_nextafterf(-0.0f, INFINITY), FLT_TRUE_MIN),
              "nextupf ~ signed-zero -> FLT_TRUE_MIN");
        puts("EQUIV nextupf: nextafterf(x,+Inf) exercised");
    }

    /* 3. nextupl --------------------------------------------------------- */
    note_skip("nextupl",
              "no cplus_nextupl; llvmorg-20.1.1 nextupl.cpp -> "
              "fputil::nextupdown<false> (src/__support C++ template)");
    {
        long double u = nextafterl(1.0L, INFINITY);
        check(same_ldouble(cplus_nextafterl(1.0L, INFINITY), u) &&
                  same_ldouble(u, 1.0L + LDBL_EPSILON) && u > 1.0L,
              "nextupl ~ nextafterl(x,+Inf)");
        check(same_ldouble(cplus_nextafterl(0.0L, INFINITY), LDBL_TRUE_MIN) &&
                  same_ldouble(cplus_nextafterl(-0.0L, INFINITY),
                               LDBL_TRUE_MIN),
              "nextupl ~ signed-zero -> LDBL_TRUE_MIN");
        puts("EQUIV nextupl: nextafterl(x,+Inf) exercised");
    }

    /* 4. nextdown -------------------------------------------------------- */
    note_skip("nextdown",
              "no cplus_nextdown; llvmorg-20.1.1 nextdown.cpp -> "
              "fputil::nextupdown<true> (src/__support C++ template)");
    {
        double d = nextafter(1.0, -INFINITY);
        check(same_double(cplus_nextafter(1.0, -INFINITY), d) &&
                  same_double(d, 1.0 - DBL_EPSILON / 2.0) && d < 1.0,
              "nextdown ~ nextafter(x,-Inf) double");
        check(same_double(cplus_nextafter(-0.0, -INFINITY), -DBL_TRUE_MIN) &&
                  same_double(cplus_nextafter(0.0, -INFINITY), -DBL_TRUE_MIN),
              "nextdown ~ signed-zero -> -DBL_TRUE_MIN");
        puts("EQUIV nextdown: nextafter(x,-Inf) exercised");
    }

    /* 5. nextdownf ------------------------------------------------------- */
    note_skip("nextdownf",
              "no cplus_nextdownf; llvmorg-20.1.1 nextdownf.cpp -> "
              "fputil::nextupdown<true> (src/__support C++ template)");
    {
        float d = nextafterf(1.0f, -INFINITY);
        check(same_float(cplus_nextafterf(1.0f, -INFINITY), d) &&
                  same_float(d, 1.0f - FLT_EPSILON / 2.0f) && d < 1.0f,
              "nextdownf ~ nextafterf(x,-Inf)");
        check(same_float(cplus_nextafterf(-0.0f, -INFINITY), -FLT_TRUE_MIN),
              "nextdownf ~ -0 -> -FLT_TRUE_MIN");
        puts("EQUIV nextdownf: nextafterf(x,-Inf) exercised");
    }

    /* 6. nextdownl ------------------------------------------------------- */
    note_skip("nextdownl",
              "no cplus_nextdownl; llvmorg-20.1.1 nextdownl.cpp -> "
              "fputil::nextupdown<true> (src/__support C++ template)");
    {
        long double d = nextafterl(1.0L, -INFINITY);
        check(same_ldouble(cplus_nextafterl(1.0L, -INFINITY), d) &&
                  same_ldouble(d, 1.0L - LDBL_EPSILON / 2.0L) && d < 1.0L,
              "nextdownl ~ nextafterl(x,-Inf)");
        check(same_ldouble(cplus_nextafterl(-0.0L, -INFINITY),
                           -LDBL_TRUE_MIN),
              "nextdownl ~ -0 -> -LDBL_TRUE_MIN");
        puts("EQUIV nextdownl: nextafterl(x,-Inf) exercised");
    }

    /* 7. fadd ------------------------------------------------------------ */
    note_skip("fadd",
              "no cplus_fadd; llvmorg-20.1.1 fadd.cpp -> "
              "fputil::generic::add<float> in src/__support/FPUtil/generic/"
              "add_sub.h; upstream `float fadd(double,double)` != C23 "
              "`double fadd(double,double)`");
    {
        double a = 1.25, b = 2.5;
        check(same_double(a + b, 3.75) &&
                  same_double(cplus_fma(a, 1.0, b), a + b),
              "fadd ~ double + and cplus_fma(a,1,b)");
        puts("EQUIV fadd: host operator+ / cplus_fma exercised");
    }

    /* 8. faddf ----------------------------------------------------------- */
    note_skip("faddf",
              "no cplus_faddf; generic/faddf.cpp is HTTP 404 at "
              "llvmorg-20.1.1 (no single-precision entry point); add_sub.h "
              "C++ dependency also blocked");
    {
        float a = 1.25f, b = 2.5f;
        check(same_float(a + b, 3.75f) &&
                  same_float(cplus_fmaf(a, 1.0f, b), a + b),
              "faddf ~ float + and cplus_fmaf(a,1,b)");
        puts("EQUIV faddf: host operator+ / cplus_fmaf exercised");
    }

    /* 9. faddl ----------------------------------------------------------- */
    note_skip("faddl",
              "no cplus_faddl; llvmorg-20.1.1 faddl.cpp -> "
              "fputil::generic::add<float> (add_sub.h) and returns float "
              "from long double args, not C23 long double faddl");
    {
        long double a = 1.25L, b = 2.5L;
        check(same_ldouble(a + b, 3.75L) &&
                  same_ldouble(cplus_fmal(a, 1.0L, b), a + b),
              "faddl ~ long double + and cplus_fmal(a,1,b)");
        puts("EQUIV faddl: host operator+ / cplus_fmal exercised");
    }

    /* 10. fsub ----------------------------------------------------------- */
    note_skip("fsub",
              "no cplus_fsub; llvmorg-20.1.1 fsub.cpp -> "
              "fputil::generic::sub<float> (add_sub.h); signature mismatch "
              "vs C23 double fsub(double,double)");
    {
        double a = 2.5, b = 1.25;
        check(same_double(a - b, 1.25) &&
                  same_double(cplus_fma(b, -1.0, a), a - b),
              "fsub ~ double - and cplus_fma(b,-1,a)");
        puts("EQUIV fsub: host operator- / cplus_fma exercised");
    }

    /* 11. fsubf ---------------------------------------------------------- */
    note_skip("fsubf",
              "no cplus_fsubf; generic/fsubf.cpp is HTTP 404 at "
              "llvmorg-20.1.1 (no single-precision entry point); add_sub.h "
              "C++ dependency also blocked");
    {
        float a = 2.5f, b = 1.25f;
        check(same_float(a - b, 1.25f) &&
                  same_float(cplus_fmaf(b, -1.0f, a), a - b),
              "fsubf ~ float - and cplus_fmaf(b,-1,a)");
        puts("EQUIV fsubf: host operator- / cplus_fmaf exercised");
    }

    /* 12. fsubl ---------------------------------------------------------- */
    note_skip("fsubl",
              "no cplus_fsubl; llvmorg-20.1.1 fsubl.cpp -> "
              "fputil::generic::sub<float> (add_sub.h); signature mismatch "
              "vs C23 long double fsubl");
    {
        long double a = 2.5L, b = 1.25L;
        check(same_ldouble(a - b, 1.25L) &&
                  same_ldouble(cplus_fmal(b, -1.0L, a), a - b),
              "fsubl ~ long double - and cplus_fmal(b,-1,a)");
        puts("EQUIV fsubl: host operator- / cplus_fmal exercised");
    }

    /* 13. fmul ----------------------------------------------------------- */
    note_skip("fmul",
              "no cplus_fmul; llvmorg-20.1.1 fmul.cpp -> "
              "fputil::generic::mul<float> plus exact_mult/DoubleDouble in "
              "src/__support/FPUtil/{mul.h,double_double.h}; signature "
              "mismatch vs C23");
    {
        double a = 1.5, b = 2.0;
        check(same_double(a * b, 3.0) &&
                  same_double(cplus_fma(a, b, 0.0), a * b),
              "fmul ~ double * and cplus_fma(a,b,0)");
        puts("EQUIV fmul: host operator* / cplus_fma exercised");
    }

    /* 14. fmulf ---------------------------------------------------------- */
    note_skip("fmulf",
              "no cplus_fmulf; generic/fmulf.cpp is HTTP 404 at "
              "llvmorg-20.1.1 (no single-precision entry point); mul.h / "
              "double_double.h C++ dependency also blocked");
    {
        float a = 1.5f, b = 2.0f;
        check(same_float(a * b, 3.0f) &&
                  same_float(cplus_fmaf(a, b, 0.0f), a * b),
              "fmulf ~ float * and cplus_fmaf(a,b,0)");
        puts("EQUIV fmulf: host operator* / cplus_fmaf exercised");
    }

    /* 15. fmull ---------------------------------------------------------- */
    note_skip("fmull",
              "no cplus_fmull; llvmorg-20.1.1 fmull.cpp -> "
              "fputil::generic::mul<float> (mul.h DyadicFloat/quick_mul); "
              "signature mismatch vs C23 long double fmull");
    {
        long double a = 1.5L, b = 2.0L;
        check(same_ldouble(a * b, 3.0L) &&
                  same_ldouble(cplus_fmal(a, b, 0.0L), a * b),
              "fmull ~ long double * and cplus_fmal(a,b,0)");
        puts("EQUIV fmull: host operator* / cplus_fmal exercised");
    }

    /* 16. fdiv ----------------------------------------------------------- */
    note_skip("fdiv",
              "no cplus_fdiv; llvmorg-20.1.1 fdiv.cpp -> "
              "fputil::generic::div<float> in src/__support/FPUtil/generic/"
              "div.h (DyadicFloat long division); signature mismatch vs C23");
    {
        double a = 3.0, b = 2.0;
        check(same_double(a / b, 1.5) &&
                  same_double(cplus_fma(a, 1.0 / b, 0.0), a / b),
              "fdiv ~ double / and cplus_fma(a,1/b,0)");
        puts("EQUIV fdiv: host operator/ / cplus_fma exercised");
    }

    /* 17. fdivf ---------------------------------------------------------- */
    note_skip("fdivf",
              "no cplus_fdivf; generic/fdivf.cpp is HTTP 404 at "
              "llvmorg-20.1.1 (no single-precision entry point); div.h C++ "
              "dependency also blocked");
    {
        float a = 3.0f, b = 2.0f;
        check(same_float(a / b, 1.5f) &&
                  same_float(cplus_fmaf(a, 1.0f / b, 0.0f), a / b),
              "fdivf ~ float / and cplus_fmaf(a,1/b,0)");
        puts("EQUIV fdivf: host operator/ / cplus_fmaf exercised");
    }

    /* 18. fdivl ---------------------------------------------------------- */
    note_skip("fdivl",
              "no cplus_fdivl; llvmorg-20.1.1 fdivl.cpp -> "
              "fputil::generic::div<float> (div.h); signature mismatch vs "
              "C23 long double fdivl");
    {
        long double a = 3.0L, b = 2.0L;
        check(same_ldouble(a / b, 1.5L) &&
                  same_ldouble(cplus_fmal(a, 1.0L / b, 0.0L), a / b),
              "fdivl ~ long double / and cplus_fmal(a,1/b,0)");
        puts("EQUIV fdivl: host operator/ / cplus_fmal exercised");
    }

    /* 19. hypot (3-argument C23 overload) -------------------------------- */
    note_skip("hypot (3-argument C23 overload)",
              "no cplus_hypot3; llvmorg-20.1.1 hypot.cpp/hypotf.cpp are "
              "2-arg only via fputil::hypot (FPUtil/Hypot.h) and "
              "src/math/hypot.h declares hypot(double,double); hypot3.cpp "
              "is HTTP 404 (no upstream source at all)");
    {
        double nested = hypot(hypot(3.0, 4.0), 12.0);
        double wrapped = cplus_hypot(cplus_hypot(3.0, 4.0), 12.0);
        check(same_double(nested, 13.0) && same_double(wrapped, nested),
              "hypot3 ~ hypot(hypot(x,y),z)");
        check(isinf(cplus_hypot(cplus_hypot(INFINITY, NAN), 1.0)) &&
                  isinf(hypot(hypot(INFINITY, NAN), 1.0)),
              "hypot3 ~ Inf/NaN propagation");
        puts("EQUIV hypot3: nested 2-arg hypot exercised");
    }

    /* 20. frexp16 -------------------------------------------------------- */
    note_skip("frexp16",
              "no cplus_frexp16/_Float16 type on this toolchain; "
              "llvmorg-20.1.1 frexpf16.cpp -> fputil::frexp in "
              "src/__support/FPUtil/ManipulationFunctions.h (FPBits / "
              "NormalFloat bit-field reclassification)");
    {
        int e = 0, ec = 0, ef = 0, efc = 0;
        double m = frexp(0.75, &e); /* 0.75 is binary16-exact */
        double mc = cplus_frexp(0.75, &ec);
        float mf = frexpf(1.5f, &ef); /* 1.5 is binary16-exact */
        float mfc = cplus_frexpf(1.5f, &efc);
        check(same_double(m, 0.75) && same_double(mc, m) && e == 0 && ec == e,
              "frexp16 ~ frexp(0.75)");
        check(same_float(mf, 0.75f) && same_float(mfc, mf) && ef == 1 &&
                  efc == ef,
              "frexp16 ~ frexpf(1.5f)");
        puts("EQUIV frexp16: frexp/frexpf on binary16-exact inputs exercised");
    }

    /* 21. fma16 ---------------------------------------------------------- */
    note_skip("fma16",
              "no cplus_fma16/_Float16 type on this toolchain; "
              "generic/fmaf16.cpp is HTTP 404 at llvmorg-20.1.1 and the "
              "closest file f16fma.cpp -> fputil::fma<float16> "
              "(src/__support/FPUtil/FMA.h) with signature "
              "`float16 f16fma(double,double,double)` != C23 fma16");
    {
        double f = fma(1.5, 2.0, 0.5); /* all operands binary16-exact */
        float ff = fmaf(1.5f, 2.0f, 0.5f);
        check(same_double(f, 3.5) && same_double(cplus_fma(1.5, 2.0, 0.5), f),
              "fma16 ~ fma(1.5,2.0,0.5)");
        check(same_float(ff, 3.5f) &&
                  same_float(cplus_fmaf(1.5f, 2.0f, 0.5f), ff),
              "fma16 ~ fmaf(1.5f,2.0f,0.5f)");
        puts("EQUIV fma16: fma/fmaf on binary16-exact inputs exercised");
    }

    /* 22. logb16 --------------------------------------------------------- */
    note_skip("logb16",
              "no cplus_logb16/_Float16 type on this toolchain; "
              "llvmorg-20.1.1 logbf16.cpp -> fputil::logb in "
              "src/__support/FPUtil/ManipulationFunctions.h "
              "(FPBits / DyadicFloat::get_unbiased_exponent)");
    {
        check(same_double(logb(8.0), 3.0) &&
                  same_double(cplus_logb(8.0), 3.0) &&
                  isinf(cplus_logb(0.0)) && signbit(cplus_logb(0.0)),
              "logb16 ~ logb(8.0) and logb(0) = -Inf");
        check(same_float(logbf(8.0f), 3.0f) &&
                  same_float(cplus_logbf(8.0f), 3.0f),
              "logb16 ~ logbf(8.0f)");
        puts("EQUIV logb16: logb/logbf on binary16-exact inputs exercised");
    }

    /* 23. exp16 ---------------------------------------------------------- */
    note_skip("exp16",
              "no cplus_exp16/_Float16 type on this toolchain; "
              "llvmorg-20.1.1 expf16.cpp is a full algorithm needing "
              "FPBits<float16>, ExceptValues, polyeval, cast, "
              "quick_get_round plus exp_range_reduction() and exception "
              "tables from expxf16.h (no self-contained source to copy)");
    {
        check(same_double(cplus_exp(0.5), exp(0.5)) &&
                  same_double(cplus_exp(-0.0), 1.0),
              "exp16 ~ exp(0.5) with tolerance");
        check(same_float(cplus_expf(0.5f), expf(0.5f)),
              "exp16 ~ expf(0.5f) with tolerance");
        puts("EQUIV exp16: exp/expf on binary16-exact inputs exercised");
    }
}

/* ======================================================================= */
/* Differential section: every linkable math wrapper vs host <math.h>       */
/* ======================================================================= */

/* --- unary, floating result --------------------------------------------- */
typedef struct {
    const char *name;
    double (*wrap)(double);
    double (*host)(double);
    double domain;
} unary_d_t;

typedef struct {
    const char *name;
    float (*wrap)(float);
    float (*host)(float);
    float domain;
} unary_f_t;

typedef struct {
    const char *name;
    long double (*wrap)(long double);
    long double (*host)(long double);
    long double domain;
} unary_l_t;

static const unary_d_t U_D[] = {
#define UD(n, dom) { #n, cplus_##n, n, (dom) }
    UD(fabs, -2.0), UD(sqrt, -1.0), UD(sin, -1.0), UD(cos, -1.0),
    UD(tan, -1.0), UD(exp, -1.0), UD(log, -1.0), UD(log10, -1.0),
    UD(floor, -1.0), UD(ceil, -1.0), UD(asin, 2.0), UD(acos, 2.0),
    UD(atan, -1.0), UD(sinh, -1.0), UD(cosh, -1.0), UD(tanh, -1.0),
    UD(log2, -1.0), UD(cbrt, -1.0), UD(round, -1.0), UD(trunc, -1.0),
    UD(asinh, -1.0), UD(acosh, -1.0), UD(atanh, 2.0), UD(log1p, -2.0),
    UD(expm1, -1.0), UD(erf, -1.0), UD(erfc, -1.0), UD(tgamma, -1.0),
    UD(lgamma, -1.0), UD(rint, -1.0), UD(logb, -1.0), UD(exp2, -1.0),
    UD(nearbyint, -1.0)
#undef UD
};

static const unary_f_t U_F[] = {
#define UF(n, dom) { #n "f", cplus_##n##f, n##f, (float)(dom) }
    UF(sin, -1.0), UF(cos, -1.0), UF(tan, -1.0), UF(asin, 2.0),
    UF(acos, 2.0), UF(atan, -1.0), UF(sinh, -1.0), UF(cosh, -1.0),
    UF(tanh, -1.0), UF(asinh, -1.0), UF(acosh, -1.0), UF(atanh, 2.0),
    UF(exp, -1.0), UF(exp2, -1.0), UF(expm1, -1.0), UF(log, -1.0),
    UF(log2, -1.0), UF(log10, -1.0), UF(log1p, -2.0), UF(sqrt, -1.0),
    UF(cbrt, -1.0), UF(floor, -1.0), UF(ceil, -1.0), UF(trunc, -1.0),
    UF(round, -1.0), UF(nearbyint, -1.0), UF(rint, -1.0), UF(fabs, -2.0),
    UF(erf, -1.0), UF(erfc, -1.0), UF(tgamma, -1.0), UF(lgamma, -1.0),
    UF(logb, -1.0)
#undef UF
};

static const unary_l_t U_L[] = {
#define UL(n, dom) { #n "l", cplus_##n##l, n##l, (long double)(dom) }
    UL(sin, -1.0), UL(cos, -1.0), UL(tan, -1.0), UL(asin, 2.0),
    UL(acos, 2.0), UL(atan, -1.0), UL(sinh, -1.0), UL(cosh, -1.0),
    UL(tanh, -1.0), UL(asinh, -1.0), UL(acosh, -1.0), UL(atanh, 2.0),
    UL(exp, -1.0), UL(exp2, -1.0), UL(expm1, -1.0), UL(log, -1.0),
    UL(log2, -1.0), UL(log10, -1.0), UL(log1p, -2.0), UL(sqrt, -1.0),
    UL(cbrt, -1.0), UL(floor, -1.0), UL(ceil, -1.0), UL(trunc, -1.0),
    UL(round, -1.0), UL(nearbyint, -1.0), UL(rint, -1.0), UL(fabs, -2.0),
    UL(erf, -1.0), UL(erfc, -1.0), UL(tgamma, -1.0), UL(lgamma, -1.0),
    UL(logb, -1.0)
#undef UL
};

static void diff_unary(void) {
    size_t i;
    int k;
    char msg[160];
    for (i = 0; i < sizeof U_D / sizeof U_D[0]; ++i) {
        double in[6] = {NAN, INFINITY, -INFINITY, 0.0, -0.0, U_D[i].domain};
        ++g_wrappers;
        for (k = 0; k < 6; ++k) {
            snprintf(msg, sizeof msg, "diff double %s in[%d]", U_D[i].name, k);
            check(same_double(U_D[i].wrap(in[k]), U_D[i].host(in[k])), msg);
        }
    }
    for (i = 0; i < sizeof U_F / sizeof U_F[0]; ++i) {
        float in[6] = {(float)NAN, INFINITY, -INFINITY, 0.0f, -0.0f,
                       U_F[i].domain};
        ++g_wrappers;
        for (k = 0; k < 6; ++k) {
            snprintf(msg, sizeof msg, "diff float %s in[%d]", U_F[i].name, k);
            check(same_float(U_F[i].wrap(in[k]), U_F[i].host(in[k])), msg);
        }
    }
    for (i = 0; i < sizeof U_L / sizeof U_L[0]; ++i) {
        long double in[6] = {(long double)NAN, INFINITY, -INFINITY, 0.0L,
                             -0.0L, U_L[i].domain};
        ++g_wrappers;
        for (k = 0; k < 6; ++k) {
            snprintf(msg, sizeof msg, "diff ldouble %s in[%d]", U_L[i].name,
                     k);
            check(same_ldouble(U_L[i].wrap(in[k]), U_L[i].host(in[k])), msg);
        }
    }
}

/* --- binary, floating result -------------------------------------------- */
typedef struct {
    const char *name;
    double (*wrap)(double, double);
    double (*host)(double, double);
} binary_d_t;

typedef struct {
    const char *name;
    float (*wrap)(float, float);
    float (*host)(float, float);
} binary_f_t;

typedef struct {
    const char *name;
    long double (*wrap)(long double, long double);
    long double (*host)(long double, long double);
} binary_l_t;

static const binary_d_t B_D[] = {
#define BD(n) { #n, cplus_##n, n }
    BD(pow), BD(fmod), BD(atan2), BD(hypot), BD(fmin), BD(fmax),
    BD(copysign), BD(nextafter), BD(fdim), BD(remainder)
#undef BD
};
static const binary_f_t B_F[] = {
#define BF(n) { #n "f", cplus_##n##f, n##f }
    BF(pow), BF(fmod), BF(atan2), BF(hypot), BF(fmin), BF(fmax),
    BF(copysign), BF(nextafter), BF(fdim), BF(remainder)
#undef BF
};
static const binary_l_t B_L[] = {
#define BL(n) { #n "l", cplus_##n##l, n##l }
    BL(pow), BL(fmod), BL(atan2), BL(hypot), BL(fmin), BL(fmax),
    BL(copysign), BL(nextafter), BL(fdim), BL(remainder)
#undef BL
};

/* NaN/Inf/-Inf/+0/-0/domain-or-negative pairs. */
static const struct { double x, y; } P_D[] = {
    {NAN, 1.0}, {1.0, NAN}, {INFINITY, 1.0}, {1.0, INFINITY},
    {-INFINITY, 1.0}, {1.0, -INFINITY}, {0.0, 1.0}, {-0.0, 1.0},
    {-1.0, 2.0}
};

static void diff_binary(void) {
    size_t i;
    int k;
    char msg[160];
    for (i = 0; i < sizeof B_D / sizeof B_D[0]; ++i) {
        ++g_wrappers;
        for (k = 0; k < (int)(sizeof P_D / sizeof P_D[0]); ++k) {
            snprintf(msg, sizeof msg, "diff double %s pair[%d]", B_D[i].name,
                     k);
            check(same_double(B_D[i].wrap(P_D[k].x, P_D[k].y),
                              B_D[i].host(P_D[k].x, P_D[k].y)),
                  msg);
        }
    }
    for (i = 0; i < sizeof B_F / sizeof B_F[0]; ++i) {
        int j;
        ++g_wrappers;
        for (j = 0; j < (int)(sizeof P_D / sizeof P_D[0]); ++j) {
            float x = (float)P_D[j].x, y = (float)P_D[j].y;
            snprintf(msg, sizeof msg, "diff float %s pair[%d]", B_F[i].name,
                     j);
            check(same_float(B_F[i].wrap(x, y), B_F[i].host(x, y)), msg);
        }
    }
    for (i = 0; i < sizeof B_L / sizeof B_L[0]; ++i) {
        int j;
        ++g_wrappers;
        for (j = 0; j < (int)(sizeof P_D / sizeof P_D[0]); ++j) {
            long double x = (long double)P_D[j].x, y = (long double)P_D[j].y;
            snprintf(msg, sizeof msg, "diff ldouble %s pair[%d]", B_L[i].name,
                     j);
            check(same_ldouble(B_L[i].wrap(x, y), B_L[i].host(x, y)), msg);
        }
    }
}

/* --- ternary fma --------------------------------------------------------- */
static void diff_fma(void) {
    static const struct { double x, y, z; } T[] = {
        {NAN, 1.0, 1.0}, {1.0, NAN, 1.0}, {INFINITY, 0.0, 1.0},
        {0.0, INFINITY, 1.0}, {-0.0, 1.0, 0.0}, {1.0, 1.0, -1.0},
        {1.5, 2.0, 0.5}
    };
    int i;
    char msg[160];
    ++g_wrappers;
    for (i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
        snprintf(msg, sizeof msg, "diff double fma triple[%d]", i);
        check(same_double(cplus_fma(T[i].x, T[i].y, T[i].z),
                          fma(T[i].x, T[i].y, T[i].z)), msg);
    }
    ++g_wrappers;
    for (i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
        float x = (float)T[i].x, y = (float)T[i].y, z = (float)T[i].z;
        snprintf(msg, sizeof msg, "diff float fmaf triple[%d]", i);
        check(same_float(cplus_fmaf(x, y, z), fmaf(x, y, z)), msg);
    }
    ++g_wrappers;
    for (i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
        long double x = (long double)T[i].x, y = (long double)T[i].y;
        long double z = (long double)T[i].z;
        snprintf(msg, sizeof msg, "diff ldouble fmal triple[%d]", i);
        check(same_ldouble(cplus_fmal(x, y, z), fmal(x, y, z)), msg);
    }
}

/* --- integer-exponent families ------------------------------------------ */
typedef struct {
    const char *name;
    double (*wrap)(double, int);
    double (*host)(double, int);
} iexp_d_t;

typedef struct {
    const char *name;
    float (*wrap)(float, int);
    float (*host)(float, int);
} iexp_f_t;

typedef struct {
    const char *name;
    long double (*wrap)(long double, int);
    long double (*host)(long double, int);
} iexp_l_t;

static const iexp_d_t IE_D[] = {
#define IED(n) { #n, cplus_##n, n }
    IED(ldexp), IED(scalbn)
#undef IED
};
static const iexp_f_t IE_F[] = {
#define IEF(n) { #n "f", cplus_##n##f, n##f }
    IEF(ldexp), IEF(scalbn)
#undef IEF
};
static const iexp_l_t IE_L[] = {
#define IEL(n) { #n "l", cplus_##n##l, n##l }
    IEL(ldexp), IEL(scalbn)
#undef IEL
};

static void diff_int_exponent(void) {
    static const int exps[] = {0, 10, -10, 1000};
    size_t i;
    int k, j;
    char msg[160];
    static const double xs[] = {NAN, INFINITY, -INFINITY, 0.0, -0.0, -1.0};
    for (i = 0; i < sizeof IE_D / sizeof IE_D[0]; ++i) {
        ++g_wrappers;
        for (k = 0; k < (int)(sizeof xs / sizeof xs[0]); ++k)
            for (j = 0; j < (int)(sizeof exps / sizeof exps[0]); ++j) {
                snprintf(msg, sizeof msg, "diff double %s x%d e%d",
                         IE_D[i].name, k, j);
                check(same_double(IE_D[i].wrap(xs[k], exps[j]),
                                  IE_D[i].host(xs[k], exps[j])), msg);
            }
    }
    for (i = 0; i < sizeof IE_F / sizeof IE_F[0]; ++i) {
        ++g_wrappers;
        for (k = 0; k < (int)(sizeof xs / sizeof xs[0]); ++k)
            for (j = 0; j < (int)(sizeof exps / sizeof exps[0]); ++j) {
                float x = (float)xs[k];
                snprintf(msg, sizeof msg, "diff float %s x%d e%d",
                         IE_F[i].name, k, j);
                check(same_float(IE_F[i].wrap(x, exps[j]),
                                 IE_F[i].host(x, exps[j])), msg);
            }
    }
    for (i = 0; i < sizeof IE_L / sizeof IE_L[0]; ++i) {
        ++g_wrappers;
        for (k = 0; k < (int)(sizeof xs / sizeof xs[0]); ++k)
            for (j = 0; j < (int)(sizeof exps / sizeof exps[0]); ++j) {
                long double x = (long double)xs[k];
                snprintf(msg, sizeof msg, "diff ldouble %s x%d e%d",
                         IE_L[i].name, k, j);
                check(same_ldouble(IE_L[i].wrap(x, exps[j]),
                                   IE_L[i].host(x, exps[j])), msg);
            }
    }
    /* scalbln / scalblnf / scalblnl take a long exponent. */
    {
        static const long les[] = {0L, 10L, -10L, 1000L};
        for (k = 0; k < (int)(sizeof xs / sizeof xs[0]); ++k)
            for (j = 0; j < (int)(sizeof les / sizeof les[0]); ++j) {
                snprintf(msg, sizeof msg, "diff double scalbln x%d e%d", k, j);
                check(same_double(cplus_scalbln(xs[k], les[j]),
                                  scalbln(xs[k], les[j])), msg);
                snprintf(msg, sizeof msg, "diff float scalblnf x%d e%d", k, j);
                check(same_float(cplus_scalblnf((float)xs[k], les[j]),
                                 scalblnf((float)xs[k], les[j])), msg);
                snprintf(msg, sizeof msg, "diff ldouble scalblnl x%d e%d", k,
                         j);
                check(same_ldouble(cplus_scalblnl((long double)xs[k], les[j]),
                                   scalblnl((long double)xs[k], les[j])), msg);
            }
        ++g_wrappers;
        ++g_wrappers;
        ++g_wrappers;
    }
}

/* --- integer-result unary families -------------------------------------- */
static void diff_int_result(void) {
    static const double xs[] = {NAN, INFINITY, -INFINITY, 0.0, -0.0, -1.0};
    int k;
    char msg[160];
    for (k = 0; k < (int)(sizeof xs / sizeof xs[0]); ++k) {
        snprintf(msg, sizeof msg, "diff double ilogb x%d", k);
        check(cplus_ilogb(xs[k]) == ilogb(xs[k]), msg);
        snprintf(msg, sizeof msg, "diff double lrint x%d", k);
        check(cplus_lrint(xs[k]) == lrint(xs[k]), msg);
        snprintf(msg, sizeof msg, "diff double llrint x%d", k);
        check(cplus_llrint(xs[k]) == llrint(xs[k]), msg);
        snprintf(msg, sizeof msg, "diff double lround x%d", k);
        check(cplus_lround(xs[k]) == lround(xs[k]), msg);
        snprintf(msg, sizeof msg, "diff double llround x%d", k);
        check(cplus_llround(xs[k]) == llround(xs[k]), msg);
        snprintf(msg, sizeof msg, "diff float ilogbf x%d", k);
        check(cplus_ilogbf((float)xs[k]) == ilogbf((float)xs[k]), msg);
        snprintf(msg, sizeof msg, "diff float lrintf x%d", k);
        check(cplus_lrintf((float)xs[k]) == lrintf((float)xs[k]), msg);
        snprintf(msg, sizeof msg, "diff float llrintf x%d", k);
        check(cplus_llrintf((float)xs[k]) == llrintf((float)xs[k]), msg);
        snprintf(msg, sizeof msg, "diff float lroundf x%d", k);
        check(cplus_lroundf((float)xs[k]) == lroundf((float)xs[k]), msg);
        snprintf(msg, sizeof msg, "diff float llroundf x%d", k);
        check(cplus_llroundf((float)xs[k]) == llroundf((float)xs[k]), msg);
        snprintf(msg, sizeof msg, "diff ldouble ilogbl x%d", k);
        check(cplus_ilogbl((long double)xs[k]) ==
                  ilogbl((long double)xs[k]), msg);
        snprintf(msg, sizeof msg, "diff ldouble lrintl x%d", k);
        check(cplus_lrintl((long double)xs[k]) ==
                  lrintl((long double)xs[k]), msg);
        snprintf(msg, sizeof msg, "diff ldouble llrintl x%d", k);
        check(cplus_llrintl((long double)xs[k]) ==
                  llrintl((long double)xs[k]), msg);
        snprintf(msg, sizeof msg, "diff ldouble lroundl x%d", k);
        check(cplus_lroundl((long double)xs[k]) ==
                  lroundl((long double)xs[k]), msg);
        snprintf(msg, sizeof msg, "diff ldouble llroundl x%d", k);
        check(cplus_llroundl((long double)xs[k]) ==
                  llroundl((long double)xs[k]), msg);
    }
    g_wrappers += 15;
}

/* --- out-parameter families --------------------------------------------- */
static void diff_out_params(void) {
    static const double xs[] = {NAN, INFINITY, -INFINITY, 0.0, -0.0, 1.5};
    int k;
    char msg[160];
    for (k = 0; k < (int)(sizeof xs / sizeof xs[0]); ++k) {
        int ec = 0, eh = 0, qc = 0, qh = 0;
        double fc, fh, ic, ih;
        float fcf, fhf, icf, ihf;
        int ecf = 0, ehf = 0, qcf = 0, qhf = 0;
        long double fcl, fhl, icl, ihl;
        int ecl = 0, ehl = 0, qcl = 0, qhl = 0;
        double x = xs[k];
        float xf = (float)xs[k];
        long double xl = (long double)xs[k];

        fc = cplus_frexp(x, &ec);
        fh = frexp(x, &eh);
        snprintf(msg, sizeof msg, "diff double frexp x%d", k);
        check(same_double(fc, fh) &&
                  (isnan(x) || isinf(x) || ec == eh), msg);

        ic = cplus_modf(x, &fc);
        ih = modf(x, &fh);
        snprintf(msg, sizeof msg, "diff double modf x%d", k);
        check(same_double(ic, ih) && same_double(fc, fh), msg);

        ic = cplus_remquo(1.5, 1.0, &qc);
        ih = remquo(1.5, 1.0, &qh);
        snprintf(msg, sizeof msg, "diff double remquo x%d (finite pair)", k);
        check(same_double(ic, ih) && qc == qh, msg);

        fcf = cplus_frexpf(xf, &ecf);
        fhf = frexpf(xf, &ehf);
        snprintf(msg, sizeof msg, "diff float frexpf x%d", k);
        check(same_float(fcf, fhf) &&
                  (isnan(xf) || isinf(xf) || ecf == ehf), msg);

        icf = cplus_modff(xf, &fcf);
        ihf = modff(xf, &fhf);
        snprintf(msg, sizeof msg, "diff float modff x%d", k);
        check(same_float(icf, ihf) && same_float(fcf, fhf), msg);

        icf = cplus_remquof(1.5f, 1.0f, &qcf);
        ihf = remquof(1.5f, 1.0f, &qhf);
        snprintf(msg, sizeof msg, "diff float remquof x%d (finite pair)", k);
        check(same_float(icf, ihf) && qcf == qhf, msg);

        fcl = cplus_frexpl(xl, &ecl);
        fhl = frexpl(xl, &ehl);
        snprintf(msg, sizeof msg, "diff ldouble frexpl x%d", k);
        check(same_ldouble(fcl, fhl) &&
                  (isnan(xl) || isinf(xl) || ecl == ehl), msg);

        icl = cplus_modfl(xl, &fcl);
        ihl = modfl(xl, &fhl);
        snprintf(msg, sizeof msg, "diff ldouble modfl x%d", k);
        check(same_ldouble(icl, ihl) && same_ldouble(fcl, fhl), msg);

        icl = cplus_remquol(1.5L, 1.0L, &qcl);
        ihl = remquol(1.5L, 1.0L, &qhl);
        snprintf(msg, sizeof msg, "diff ldouble remquol x%d (finite pair)", k);
        check(same_ldouble(icl, ihl) && qcl == qhl, msg);
    }
    g_wrappers += 9;
}

/* --- nan / nexttoward / exp10 / roundeven -------------------------------- */
static void diff_special_families(void) {
    static const struct { double x; long double y; } NT[] = {
        {NAN, 1.0L}, {1.0, (long double)NAN}, {INFINITY, 1.0L},
        {1.0, INFINITY}, {-INFINITY, 1.0L}, {0.0, -1.0L}, {-0.0, 1.0L}
    };
    static const double ex[] = {NAN, INFINITY, -INFINITY, 0.0, -0.0, 0.5,
                                -1.0};
    int i, oldround;
    char msg[160];

    check(isnan(cplus_nan("")) && isnan(nan("")) &&
              isnan(cplus_nanf("")) && isnan(nanf("")) &&
              isnan(cplus_nanl("")) && isnan(nanl("")),
          "diff nan/nanf/nanl classification");
    g_wrappers += 3;

    for (i = 0; i < (int)(sizeof NT / sizeof NT[0]); ++i) {
        snprintf(msg, sizeof msg, "diff double nexttoward case %d", i);
        check(same_double(cplus_nexttoward(NT[i].x, NT[i].y),
                          nexttoward(NT[i].x, NT[i].y)), msg);
        snprintf(msg, sizeof msg, "diff float nexttowardf case %d", i);
        check(same_float(cplus_nexttowardf((float)NT[i].x, NT[i].y),
                         nexttowardf((float)NT[i].x, NT[i].y)), msg);
        snprintf(msg, sizeof msg, "diff ldouble nexttowardl case %d", i);
        check(same_ldouble(cplus_nexttowardl((long double)NT[i].x, NT[i].y),
                           nexttowardl((long double)NT[i].x, NT[i].y)), msg);
    }
    g_wrappers += 3;

    /* exp10 has no host symbol on this SDK; pow(10,x) is the oracle and the
     * tolerance absorbs the musl 10^n table / exp2 scaling path. */
    for (i = 0; i < (int)(sizeof ex / sizeof ex[0]); ++i) {
        snprintf(msg, sizeof msg, "diff double exp10 case %d", i);
        check(same_double(cplus_exp10(ex[i]), pow(10.0, ex[i])), msg);
        snprintf(msg, sizeof msg, "diff float exp10f case %d", i);
        check(same_float(cplus_exp10f((float)ex[i]),
                         powf(10.0f, (float)ex[i])), msg);
        snprintf(msg, sizeof msg, "diff ldouble exp10l case %d", i);
        check(same_ldouble(cplus_exp10l((long double)ex[i]),
                           powl(10.0L, (long double)ex[i])), msg);
    }
    g_wrappers += 3;

    /* roundeven has no host symbol; nearbyint under FE_TONEAREST is the
     * nearest available oracle (both must be tie-to-even here). */
    oldround = fegetround();
    check(fesetround(FE_TONEAREST) == 0, "diff roundeven set FE_TONEAREST");
    for (i = 0; i < (int)(sizeof ex / sizeof ex[0]); ++i) {
        snprintf(msg, sizeof msg, "diff double roundeven case %d", i);
        check(same_double(cplus_roundeven(ex[i]), nearbyint(ex[i])), msg);
        snprintf(msg, sizeof msg, "diff float roundevenf case %d", i);
        check(same_float(cplus_roundevenf((float)ex[i]),
                         nearbyintf((float)ex[i])), msg);
        snprintf(msg, sizeof msg, "diff ldouble roundevenl case %d", i);
        check(same_ldouble(cplus_roundevenl((long double)ex[i]),
                           nearbyintl((long double)ex[i])), msg);
    }
    check(fesetround(oldround) == 0, "diff roundeven restore rounding mode");
    g_wrappers += 3;
}

int main(void) {
    exercise_gap_equivalents();

    diff_unary();
    diff_binary();
    diff_fma();
    diff_int_exponent();
    diff_int_result();
    diff_out_params();
    diff_special_families();

    if (g_skipped != 23) {
        printf("FAIL skip count: expected 23, got %d\n", g_skipped);
        g_failed = 1;
    }
    printf("math_gaps: %d tested, %d skipped\n", g_tested, g_skipped);
    printf("math_gaps: differential wrappers covered: %d\n", g_wrappers);
    printf("math_gaps: %s\n", g_failed ? "FAILED" : "ok");
    return g_failed ? 1 : 0;
}
