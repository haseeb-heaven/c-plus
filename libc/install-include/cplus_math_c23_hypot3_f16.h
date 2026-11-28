/* C+ hardened libc: C23 <math.h> hypot3 / _Float16 closure surface.
 *
 * Declares the five C23 math APIs implemented in
 * libc/src/math/math_c23_hypot3_f16.c. These are ADDITIVE entry points: they
 * were previously declared-only in math_c23.c / math_c23_upstream.c, so no
 * existing signature changes.
 *
 *   double   cplus_hypot3(double, double, double);
 *   _Float16 cplus_frexp16(_Float16, int *);
 *   _Float16 cplus_fma16(_Float16, _Float16, _Float16);
 *   _Float16 cplus_logb16(_Float16);
 *   _Float16 cplus_exp16(_Float16);
 *
 * Backend, probe evidence and every semantic deviation are documented in
 * docs/libc/C23_HYPOT_F16_CLOSURE.md. The four _Float16 declarations are
 * emitted only when the compiler provides the _Float16 type (guard mirrors
 * math_c23.c); this host's Apple Clang 16 does (__FLT16_MANT_DIG__ == 11).
 */
#ifndef CPLUS_MATH_C23_HYPOT3_F16_H
#define CPLUS_MATH_C23_HYPOT3_F16_H

#ifdef __cplusplus
extern "C" {
#endif

/* C23 3-argument hypot: Euclidean norm sqrt(x*x + y*y + z*z) computed
 * without intermediate overflow. Implemented as hypot(hypot(x,y),z) using
 * the host's overflow-safe 2-argument hypot; may differ ~1 ulp from a
 * hypothetical single-step correctly-rounded 3-arg result (see closure doc). */
double cplus_hypot3(double x, double y, double z);

#if defined(__FLT16_MANT_DIG__)
/* frexp16: decompose x into a significand in [0.5,1) (returned) and an
 * integer exponent (written through exp). Exact for all _Float16 inputs. */
_Float16 cplus_frexp16(_Float16 x, int *exp);

/* fma16: fused multiply-add x*y + z with a single rounding; correctly
 * rounded for all _Float16 inputs. */
_Float16 cplus_fma16(_Float16 x, _Float16 y, _Float16 z);

/* logb16: floor(log2(|x|)) as a _Float16 (integer-valued); exact. */
_Float16 cplus_logb16(_Float16 x);

/* exp16: e raised to x, returned as _Float16. Correctly rounded for the
 * probed domain; on some hosts the builtin lowers via the float expf symbol. */
_Float16 cplus_exp16(_Float16 x);
#endif /* __FLT16_MANT_DIG__ */

#ifdef __cplusplus
}
#endif
#endif /* CPLUS_MATH_C23_HYPOT3_F16_H */
