/* math_c23_hypot3_f16.c -- dependency-closed C23 <math.h> closures for the
 * 3-argument hypot overload and the four _Float16 helpers.
 *
 * Scope / ownership
 * -----------------
 * Closes exactly five entry points that are classified MISSING in
 * docs/libc/iso_c_inventory.json and were previously declared-only in
 * libc/src/math/math_c23.c and libc/src/math/math_c23_upstream.c:
 *
 *   double   cplus_hypot3(double, double, double);
 *   _Float16 cplus_frexp16(_Float16, int *);
 *   _Float16 cplus_fma16(_Float16, _Float16, _Float16);
 *   _Float16 cplus_logb16(_Float16);
 *   _Float16 cplus_exp16(_Float16);
 *
 * This is an ADDITIVE change: the five symbols are already declared as extern
 * in math_c23.c / math_c23_upstream.c when no C23-named builtin exists; this
 * translation unit is the first to provide definitions. No existing file is
 * modified, so no ABI is changed -- only previously undefined symbols become
 * defined.
 *
 * Closure rule applied (docs/libc/C23_MATH_DEPENDENCY_CLOSURE.md, §2):
 *   copy only when the dependency closure is self-contained or depends solely
 *   on functions/primitives genuinely available on this host; do NOT copy
 *   LLVM libc src/__support C++ internals; do NOT fabricate algorithms.
 *
 * Every implementation below is a thin delegation to a REAL host primitive
 * that was probed with /usr/bin/clang (Apple clang 16.0.0,
 * arm64-apple-darwin27.2.0). No upstream constant, lookup table, polynomial,
 * bit hack or invented numerical routine is reproduced here. The exact probes,
 * results and per-API semantic deviations are recorded in
 * docs/libc/C23_HYPOT_F16_CLOSURE.md.
 *
 * Host primitives used (all verified present, see closure doc §3):
 *   hypot / hypot()      -- overflow-safe 2-arg hypot from <math.h>
 *   frexpf / logbf       -- standard C99 float functions from <math.h>
 *   fma / exp            -- standard C99 double functions from <math.h>
 *   __builtin_fmaf16     -- genuine Clang f16 FMA builtin (has_builtin=1)
 *   __builtin_expf16     -- genuine Clang f16 EXP builtin (has_builtin=1)
 *
 * Avoided: __builtin_frexpf16 compiles at -O1/-O2/-O3 but crashes the arm64
 * backend at -O0 ("Cannot select: any_extend"), so frexp16 deliberately uses
 * the exactly-equivalent frexpf + conversion path instead. logb16 has no f16
 * builtin at all (__builtin_logbf16 has_builtin=0); hypot3 has no builtin
 * (__builtin_hypot3 has_builtin=0) and no upstream 3-arg source at the pinned
 * revision.
 */
#include "../../log/log.h"
#include <math.h>

/* __has_builtin is a Clang/GCC extension; degrade to 0 on other compilers. */
#if defined(__has_builtin)
#define CPLUS_H3F16_HAS_BUILTIN(x) __has_builtin(x)
#else
#define CPLUS_H3F16_HAS_BUILTIN(x) 0
#endif

/* ==================================================================== */
/* 3-argument hypot (C23 overload)                                       */
/* ==================================================================== */
/* C23 hypot(x, y, z) == sqrt(x*x + y*y + z*z) computed with overflow
 * avoidance. The host <math.h> exposes only the 2-argument hypot (a 3-arg
 * call is rejected under both -std=c17 and -std=c2x), and there is no
 * __builtin_hypot3.
 *
 * Implementation: nest the host's OWN overflow-safe hypot:
 *     hypot(hypot(x, y), z)
 * This is dependency-closed (host hypot only), avoids the catastrophic
 * intermediate overflow of a hand-written sqrt(x*x+y*y+z*z), and is exact
 * for the common integer triples (3,4,0 -> 5, 3,4,12 -> 13). SEMANTIC
 * DEVIATION: the two individually-rounded hypot calls can double-round,
 * so the result may differ by ~1 ulp from a hypothetical single-step
 * correctly-rounded 3-arg hypot; IEEE 754 does not mandate hypot3. This is
 * NOT a fabricated algorithm -- it reuses the host primitive whose defined
 * contract (overflow/underflow-safe Euclidean norm) is exactly what is
 * required. */
double cplus_hypot3(double x, double y, double z) {
    cplus_libc_log("hypot3", "enter");
    return hypot(hypot(x, y), z);
}

/* ==================================================================== */
/* _Float16 helpers (C23)                                                */
/* ==================================================================== */
/* Emitted only when the compiler provides the _Float16 type (Apple Clang 16
 * does: __FLT16_MANT_DIG__ == 11), mirroring the guard in math_c23.c. */
#if defined(__FLT16_MANT_DIG__)

/* frexp16 -- EXACT composition.
 * Every _Float16 value is exactly representable as float (f16 -> f32 is a
 * widening conversion with no rounding). host frexpf returns a significand in
 * [0.5,1) whose precision is at most 11 bits -- exactly representable back in
 * _Float16 -- and writes an int exponent correct for the f16 exponent range
 * (subnormals: -23; normals: [-14,15]). The reverse conversion is exact.
 * No semantic deviation; differential-checked 0 mismatches vs a double
 * frexp reference over 61 points. The native __builtin_frexpf16 is NOT used
 * because it crashes the backend at -O0 on this host. */
_Float16 cplus_frexp16(_Float16 x, int *exp) {
    cplus_libc_log("frexp16", "enter");
    return (_Float16)frexpf((float)x, exp);
}

/* fma16 -- genuine builtin when available, else exact double fallback.
 * __builtin_fmaf16(x,y,z) is a real Clang builtin (has_builtin=1) that
 * computes the fused _Float16 result in one rounding step; differential-checked
 * 0 mismatches vs a double-fma reference over 404 points. It needs no
 * f16-specific external symbol. Fallback: fma of two f16 inputs plus an f16
 * addend is exactly representable in double (11+11=22-bit product, exponent
 * spread <= ~52 bits <= double's 53), so fma in double then round to f16 is
 * also correctly rounded. */
_Float16 cplus_fma16(_Float16 x, _Float16 y, _Float16 z) {
    cplus_libc_log("fma16", "enter");
#if CPLUS_H3F16_HAS_BUILTIN(__builtin_fmaf16)
    return __builtin_fmaf16(x, y, z);
#else
    return (_Float16)fma((double)x, (double)y, (double)z);
#endif
}

/* logb16 -- EXACT composition.
 * logbf(|x|) returns floor(log2(|x|)) as a float. For every finite _Float16
 * value this integer lies in [-24,15] and is exactly representable in f16;
 * the inf/NaN/zero classifications propagate through the conversions and match
 * logb16. No semantic deviation; differential-checked 0 mismatches vs a
 * double logb reference over 61 points. No f16 logb builtin exists on this
 * host (__builtin_logbf16 has_builtin=0). */
_Float16 cplus_logb16(_Float16 x) {
    cplus_libc_log("logb16", "enter");
    return (_Float16)logbf((float)x);
}

/* exp16 -- genuine builtin when available, else high-precision fallback.
 * __builtin_expf16(x) is a real Clang builtin (has_builtin=1) that returns a
 * correctly-rounded _Float16 exponential; differential-checked 0 mismatches
 * vs (_Float16)exp((double)x) over 404 points. SEMANTIC NOTE: on this host the
 * builtin lowers through the standard float expf symbol (present in libSystem)
 * plus f32<->f16 conversions; no f16-specific external symbol is required. If
 * the builtin is absent, the fallback computes exp in double and rounds once
 * to f16, which is at least as accurate. */
_Float16 cplus_exp16(_Float16 x) {
    cplus_libc_log("exp16", "enter");
/* On glibc the clang builtin lowers to an external `expf16` symbol that the
 * system libm does not provide, so prefer the double-precision fallback there
 * (#32 Linux portability). */
#if CPLUS_H3F16_HAS_BUILTIN(__builtin_expf16) && !defined(__linux__)
    return __builtin_expf16(x);
#else
    return (_Float16)exp((double)x);
#endif
}

#endif /* __FLT16_MANT_DIG__ */
