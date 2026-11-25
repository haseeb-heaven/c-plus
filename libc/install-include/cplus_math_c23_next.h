/* C+ hardened libc -- ISO C23 nextup/nextdown family declarations.
 *
 * Declares the six C23 <math.h> "next representable value" entry points closed
 * by libc/src/math/math_c23_next.c. They are exported under the project-wide
 * cplus_* namespace, matching every other public entry point in cplus_libc.h.
 *
 * The bare ISO spellings (nextup, nextupf, nextupl, nextdown, nextdownf,
 * nextdownl) are intentionally NOT declared here: the host <math.h> on this
 * toolchain does not provide them, and defining global symbols of those names
 * would collide with any future host header that does. See
 * docs/libc/C23_NEXT_CLOSURE.md for the full contract and deviation record.
 *
 * Semantics (delegated to the host libm nextafter family):
 *   cplus_nextup(x)     == nextafter(x, +INFINITY)  -- next value toward +inf
 *   cplus_nextdown(x)   == nextafter(x, -INFINITY)  -- next value toward -inf
 *   NaN input is returned unchanged (payload preserved, no FE_INVALID raised).
 *   For +/-0 the least subnormal of the corresponding direction is returned.
 */
#ifndef CPLUS_MATH_C23_NEXT_H
#define CPLUS_MATH_C23_NEXT_H

#ifdef __cplusplus
extern "C" {
#endif

double cplus_nextup(double x);
float cplus_nextupf(float x);
long double cplus_nextupl(long double x);

double cplus_nextdown(double x);
float cplus_nextdownf(float x);
long double cplus_nextdownl(long double x);

#ifdef __cplusplus
}
#endif
#endif /* CPLUS_MATH_C23_NEXT_H */
