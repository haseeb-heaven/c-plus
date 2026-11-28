/* C+ hardened libc -- standalone public header for the six ISO C23
 * nextup/nextdown APIs with their exact bare ISO names and signatures.
 *
 * These are the entries classified MISSING in docs/libc/iso_c_inventory.json.
 * Unlike the project's usual cplus_* namespace wrappers, these declarations
 * intentionally use the ISO spellings defined by C23 7.12.11.5/7.12.11.6
 * (<math.h>):
 *
 *   float       nextupf(float x);
 *   double      nextup(double x);
 *   long double nextupl(long double x);
 *   float       nextdownf(float x);
 *   double      nextdown(double x);
 *   long double nextdownl(long double x);
 *
 * Definitions live in libc/src/math/c23_nextup.c. On this host the SDK
 * <math.h> does not declare these names, so there is no redeclaration
 * conflict. On a toolchain whose <math.h> does declare them with identical
 * signatures, repeated compatible declarations are valid C.
 *
 * This header is strictly additive: no types, macros or existing declarations
 * are changed. It is safe to include alone from C99/C11/C17/C2x and from C++
 * translation units (extern "C" linkage is applied in C++ mode).
 */
#ifndef CPLUS_C23_NEXTUP_H
#define CPLUS_C23_NEXTUP_H

#ifdef __cplusplus
extern "C" {
#endif

float nextupf(float x);
double nextup(double x);
long double nextupl(long double x);

float nextdownf(float x);
double nextdown(double x);
long double nextdownl(long double x);

#ifdef __cplusplus
}
#endif

#endif /* CPLUS_C23_NEXTUP_H */
