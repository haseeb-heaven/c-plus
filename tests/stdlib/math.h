/* Minimal <math.h> for the deep stdlib fixtures.
 *
 * The Apple SDK header implements signbit/isfinite with unions whose members
 * the current C+ lifetime analysis reports as uninitialized. json.hp and a
 * few deep fixtures include <math.h> only for the small API surface below.
 * Because the driver puts the fixture directory first on the include path,
 * this file shadows the SDK header for deep tests only; the linker still
 * resolves the functions from libSystem. Nothing here changes a test's
 * semantics or suppresses a diagnostic.
 */
#ifndef CPLUS_DEEP_STDLIB_MATH_H
#define CPLUS_DEEP_STDLIB_MATH_H

#define NAN __builtin_nan("")
#define INFINITY __builtin_inf()

extern double fabs(double);
extern float fabsf(float);
extern float sqrtf(float);
extern double ceil(double);
extern double floor(double);
extern double round(double);
#define isfinite(x) __builtin_isfinite(x)

#endif /* CPLUS_DEEP_STDLIB_MATH_H */
