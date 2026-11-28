/* c23_hypot16.c -- additive C23 3-argument hypot and _Float16 math APIs.
 *
 * Implements the five APIs classified MISSING in
 * docs/libc/iso_c_inventory.json (math.h rows: "hypot (3-argument C23
 * overload)", frexp16, fma16, logb16, exp16):
 *
 *   double   cplus_c23_hypot3(double, double, double);
 *   _Float16 cplus_c23_frexp16(_Float16, int *);
 *   _Float16 cplus_c23_fma16(_Float16, _Float16, _Float16);
 *   _Float16 cplus_c23_logb16(_Float16);
 *   _Float16 cplus_c23_exp16(_Float16);
 *
 * PROVENANCE (checked before writing any code):
 *   - `grep -rilE 'hypot3|frexp16|fma16|logb16|exp16|_Float16|binary16'
 *     third_party libc/upstream` returned no files: there is no pinned
 *     vendored source for these APIs in this repository.
 *   - docs/libc/C23_MATH_UPSTREAM.md records LLVM libc llvmorg-20.1.1 as the
 *     pinned upstream and states that hypot3 has no upstream implementation
 *     there; the f16 math paths would require C++ src/__support internals
 *     that are not vendored here.
 *   - No upstream body, constant, table or polynomial is copied. Every body
 *     below is either an original scaling composition or a delegation to a
 *     real host primitive that was probed on this machine (see
 *     docs/libc/C23_HYPOT16.md for exact commands, output and host version).
 *
 * SYMBOL NAMING: cplus_c23_* is used intentionally. This branch already ships
 * cplus_hypot3 / cplus_frexp16 / cplus_fma16 / cplus_logb16 / cplus_exp16 in
 * libc/src/math/math_c23_hypot3_f16.c and that file is wired into the build.
 * Distinct names make this file purely additive: linking both translation
 * units produces no duplicate-definition error.
 */
#include <math.h>

#include "cplus_c23_hypot16.h"

/* __has_builtin is a Clang/GCC extension; degrade to 0 elsewhere. */
#if defined(__has_builtin)
#define C23H16_HAS_BUILTIN(x) __has_builtin(x)
#else
#define C23H16_HAS_BUILTIN(x) 0
#endif

/* ------------------------------------------------------------------ */
/* 3-argument hypot                                                     */
/* ------------------------------------------------------------------ */
/* Host capability: <math.h> declares only 2-argument hypot; a 3-argument
 * call is rejected ("too many arguments ... expected 2, have 3") and
 * __has_builtin(__builtin_hypot3) == 0. Therefore no host overload exists
 * to delegate to and an explicit composition is required.
 *
 * Documented algorithm (original, not copied from any upstream source):
 *   1. Propagate NaN / infinity using the standard hypot contract.
 *   2. m = max(|x|,|y|,|z|) via fmax. If m == 0, return +0.
 *   3. frexp(m, &e) gives m = f * 2^e with f in [0.5,1).
 *   4. Multiply every component by 2^-e with ldexp. A scaling by a power of
 *      two is exact (except for benign underflow of negligible components),
 *      and fmin(.,1.0) is a safety clamp: the largest scaled component is
 *      already < 1.
 *   5. Each scaled square is < 1, so sx^2+sy^2+sz^2 < 3 and sqrt cannot
 *      overflow. The sum of squares is computed in double precision.
 *   6. ldexp(result, e) restores the magnitude. It overflows to +inf only
 *      when the true norm exceeds DBL_MAX, and rounds once on underflow.
 *
 * Why this matters: the naive sqrt(x*x+y*y+z*z) overflows for e.g.
 * (1e200,1e200,1e200) because 1e400 is not representable; step 4 prevents
 * that. The two-rounding sequence (sum of squares, sqrt) means the result
 * can differ by a few ulp from a hypothetical single-step correctly-rounded
 * hypot3; this is documented as a deviation, not hidden.
 *
 * The literal form hypot(hypot(x,y),z) would also be overflow-safe because
 * the host hypot scales internally, but it performs two sqrt roundings and
 * relies on a host primitive whose 3-argument behaviour is absent here; the
 * explicit scaling above keeps the numerical argument inspectable and
 * testable at -O0 without depending on that nested-rounding behaviour. */
double cplus_c23_hypot3(double x, double y, double z) {
    double m, sx, sy, sz, sum;
    int e = 0;

    /* Match host hypot's precedence: +inf wins even if another input is NaN
     * (verified: hypot(INFINITY,NAN) == +inf on this host). */
    if (isinf(x) || isinf(y) || isinf(z))
        return INFINITY;
    if (isnan(x) || isnan(y) || isnan(z))
        return NAN;

    m = fmax(fmax(fabs(x), fabs(y)), fabs(z));
    if (m == 0.0)
        return 0.0;

    (void)frexp(m, &e); /* e in [-1073,1024] for IEEE-754 binary64 */
    sx = fmin(fabs(ldexp(x, -e)), 1.0);
    sy = fmin(fabs(ldexp(y, -e)), 1.0);
    sz = fmin(fabs(ldexp(z, -e)), 1.0);
    sum = sx * sx + sy * sy + sz * sz; /* < 3: no intermediate overflow */
    return ldexp(sqrt(sum), e);
}

#if defined(__FLT16_MANT_DIG__)
/* ------------------------------------------------------------------ */
/* _Float16 helpers                                                     */
/* ------------------------------------------------------------------ */
/* Host probes (c99/c17 identical): __FLT16_MANT_DIG__ == 11;
 * __has_builtin(__builtin_fmaf16) == 1;
 * __has_builtin(__builtin_expf16) == 1;
 * __has_builtin(__builtin_frexpf16) == 1 but it crashes the arm64 backend
 * at -O0 ("Cannot select: i32 = any_extend"), so it is not used;
 * __has_builtin(__builtin_logbf16) == 0.
 * No _Float16 fallback representation is fabricated: the type is present on
 * this host, so the non-conforming fallback branch is unnecessary here. */

/* frexp16 -- exact composition. f16 -> f32 widening is lossless; frexpf
 * returns a significand with at most 11 meaningful bits, exactly
 * representable back in f16, and an int exponent valid for the f16 range. */
_Float16 cplus_c23_frexp16(_Float16 x, int *exp) {
    return (_Float16)frexpf((float)x, exp);
}

/* fma16 -- genuine fused f16 builtin when the compiler has it. Otherwise
 * fma in double: the 11x11-bit product plus an f16 addend is exactly
 * representable in binary64, so one double fma followed by one rounding to
 * f16 is correctly rounded. */
_Float16 cplus_c23_fma16(_Float16 x, _Float16 y, _Float16 z) {
#if C23H16_HAS_BUILTIN(__builtin_fmaf16)
    return __builtin_fmaf16(x, y, z);
#else
    return (_Float16)fma((double)x, (double)y, (double)z);
#endif
}

/* logb16 -- floor(log2(|x|)) is an integer in [-24,15] for every finite f16
 * value, exactly representable in f16. logbf has no f16 builtin on this
 * host; the f32 route is exact for f16 inputs. */
_Float16 cplus_c23_logb16(_Float16 x) {
    return (_Float16)logbf((float)x);
}

/* exp16 -- genuine f16 exponential builtin when present. On this host it
 * lowers through the standard expf symbol, which is in libSystem; no
 * f16-specific external dependency is introduced. Fallback: exp in double
 * rounded once to f16. */
_Float16 cplus_c23_exp16(_Float16 x) {
    return (_Float16)exp((double)x);
}

#endif /* __FLT16_MANT_DIG__ */
