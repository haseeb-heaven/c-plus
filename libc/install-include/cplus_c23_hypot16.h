/* cplus_c23_hypot16.h -- public surface for the additive C23 math API file
 * libc/src/math/c23_hypot16.c.
 *
 * Exposes five C23 <math.h> entry points that are classified MISSING in
 * docs/libc/iso_c_inventory.json:
 *
 *   hypot three-argument overload, frexp16, fma16, logb16, exp16.
 *
 * NAMING: this translation unit deliberately uses the cplus_c23_* prefix
 * rather than reusing the already-shipped cplus_hypot3 / cplus_frexp16 /
 * cplus_fma16 / cplus_logb16 / cplus_exp16 symbols from
 * libc/src/math/math_c23_hypot3_f16.c. Both files can therefore be linked
 * into the same libc without duplicate definitions. See
 * docs/libc/C23_HYPOT16.md for the integration note.
 *
 * The four _Float16 declarations are emitted only when the compiler provides
 * the _Float16 type (__FLT16_MANT_DIG__). No non-conforming fallback type is
 * substituted; the host capability evidence is in docs/libc/C23_HYPOT16.md.
 */
#ifndef CPLUS_C23_HYPOT16_H
#define CPLUS_C23_HYPOT16_H

#ifdef __cplusplus
extern "C" {
#endif

/* C23 hypot(x,y,z): sqrt(x*x + y*y + z*z) with overflow-safe scaling.
 * Finite for every representable norm; a power-of-two scaling keeps the
 * intermediate sum of squares below 3, so 1e200 inputs do not overflow. */
double cplus_c23_hypot3(double x, double y, double z);

#if defined(__FLT16_MANT_DIG__)
/* frexp16: significand in [0.5,1) returned, binary exponent written to exp.
 * Exact for _Float16 inputs (f16 -> f32 widening is lossless). */
_Float16 cplus_c23_frexp16(_Float16 x, int *exp);

/* fma16: fused x*y + z, single rounding. */
_Float16 cplus_c23_fma16(_Float16 x, _Float16 y, _Float16 z);

/* logb16: floor(log2(|x|)) as an integer-valued _Float16. */
_Float16 cplus_c23_logb16(_Float16 x);

/* exp16: e raised to x, returned as _Float16. */
_Float16 cplus_c23_exp16(_Float16 x);
#endif /* __FLT16_MANT_DIG__ */

#ifdef __cplusplus
}
#endif
#endif /* CPLUS_C23_HYPOT16_H */
