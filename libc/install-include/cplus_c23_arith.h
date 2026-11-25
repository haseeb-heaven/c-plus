/* C+ hardened libc -- standalone declarations for the 12 C23 arithmetic
 * entry points tracked as MISSING in docs/libc/iso_c_inventory.json:
 *
 *     float cplus_fadd (double x, double y);        float cplus_faddf (float x, float y);
 *     float cplus_faddl(long double x, long double y);
 *     float cplus_fsub (double x, double y);        float cplus_fsubf (float x, float y);
 *     float cplus_fsubl(long double x, long double y);
 *     float cplus_fmul (double x, double y);        float cplus_fmulf (float x, float y);
 *     float cplus_fmull(long double x, long double y);
 *     float cplus_fdiv (double x, double y);        float cplus_fdivf (float x, float y);
 *     float cplus_fdivl(long double x, long double y);
 *
 * The eight un-suffixed / -l entry points use the ACTUAL ISO C23 signatures
 * from 7.12.14 ("Functions that round result to narrower type"): each takes
 * double or long double operands and returns float, computing the operation
 * as if to infinite precision and rounding once to float according to the
 * current rounding mode. The four /f entry points complete the family for
 * the float semantic type (float operands, float result); ISO C23 7.12.14
 * does not define those four, so their float-width signature is a documented
 * family-extension contract, not a claim of a standard spelling.
 *
 * All names carry the project-wide cplus_* namespace; the bare ISO spellings
 * are deliberately not declared because they could collide with a future
 * host <math.h>. This header is self-contained: it includes only <float.h>
 * from the C standard library and is valid in C99, C11, C17 and C++ TUs.
 *
 * PROVENANCE / LIMITATION (important, see docs/libc/C23_ARITH.md):
 * No pinned upstream implementation of these names exists in third_party/
 * (verified: `grep -rn fadd third_party/` returned no matches). The bodies in
 * libc/src/math/c23_arith.c are therefore thin delegations to the host
 * operators + - * / (and one narrowing cast for the eight C23 7.12.14
 * signatures). On the verified host FLT_EVAL_METHOD == 0, FLT_RADIX == 2 and
 * the binary precisions are 24/53/53, so the host operators are IEEE 754
 * operations that honour the current rounding mode and raise the standard
 * exceptions. This does NOT constitute a formal proof of C23 correct
 * rounding: the "as if infinite precision, single round" property of the
 * host implementation is inherited from the platform, not established here.
 */
#ifndef CPLUS_C23_ARITH_H
#define CPLUS_C23_ARITH_H

#include <float.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1 means the host exposes IEEE 754 binary operators with no excess precision
 * (FLT_EVAL_METHOD == 0) and binary formats (FLT_RADIX == 2), which is the
 * precondition for the thin host-operator wrappers to be meaningful. It is a
 * capability flag, NOT a claim of formally proven C23 correct rounding. */
#if defined(FLT_EVAL_METHOD) && FLT_EVAL_METHOD == 0 &&                       \
    defined(FLT_RADIX) && FLT_RADIX == 2
#define CPLUS_C23_ARITH_HAVE_HOST_OPERATORS 1
#else
#define CPLUS_C23_ARITH_HAVE_HOST_OPERATORS 0
#endif

/* Number of entry points implemented by c23_arith.c. */
#define CPLUS_C23_ARITH_COUNT 12

#if CPLUS_C23_ARITH_HAVE_HOST_OPERATORS

/* ISO C23 7.12.14.1 -- add and round to narrower type. */
float cplus_fadd(double x, double y);
float cplus_faddf(float x, float y);
float cplus_faddl(long double x, long double y);

/* ISO C23 7.12.14.2 -- subtract and round to narrower type. */
float cplus_fsub(double x, double y);
float cplus_fsubf(float x, float y);
float cplus_fsubl(long double x, long double y);

/* ISO C23 7.12.14.3 -- multiply and round to narrower type. */
float cplus_fmul(double x, double y);
float cplus_fmulf(float x, float y);
float cplus_fmull(long double x, long double y);

/* ISO C23 7.12.14.4 -- divide and round to narrower type. */
float cplus_fdiv(double x, double y);
float cplus_fdivf(float x, float y);
float cplus_fdivl(long double x, long double y);

#endif /* CPLUS_C23_ARITH_HAVE_HOST_OPERATORS */

#ifdef __cplusplus
}
#endif
#endif /* CPLUS_C23_ARITH_H */
