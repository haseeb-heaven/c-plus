/* C+ hardened libc -- ISO C23 fadd/fsub/fmul/fdiv (narrowing) declarations.
 *
 * Declares the eight C23 <math.h> "functions that round result to narrower
 * type" (ISO/IEC 9899:2024, 7.12.14) implemented in
 * libc/src/math/math_c23_farith.c, under the project-wide cplus_* namespace.
 *
 * SIGNATURES -- these are the ACTUAL C23 signatures. C23 7.12.14.1-.4 says:
 *     float fadd (double x, double y);       float faddl (long double x, long double y);
 *     float fsub (double x, double y);       float fsubl (long double x, long double y);
 *     float fmul (double x, double y);       float fmull (long double x, long double y);
 *     float fdiv (double x, double y);       float fdivl (long double x, long double y);
 * The return type is float; the operands are double or long double. This is a
 * NARROWING operation: "compute [the operation] (as if) to infinite precision
 * and round once to the return type, according to the current rounding mode".
 * (Earlier repository notes that claimed `double fadd(double,double)` were
 * incorrect; see docs/libc/C23_FARITH_CLOSURE.md for the normative quote.)
 *
 * The bare ISO spellings are intentionally NOT declared here: defining global
 * symbols named fadd/fsub/... would collide with a future host <math.h>.
 *
 * NOT C23 APIs: faddf, fsubf, fmulf, fdivf do not exist in ISO/IEC 9899:2024
 * (7.12.14 has no /suffix variants). They are BLOCKED and emit no declaration
 * and no symbol; see CPLUS_C23_FARITH_BLOCKER_* below.
 *
 * INTEGRATION NOTE: the stale `extern double cplus_fadd(double,double);` (and
 * sibling) declarations in libc/src/math/math_c23.c /
 * math_c23_upstream.c must be updated to the signatures above by the agent
 * that wires the new translation unit into the build. Until then this header
 * is the authoritative contract for these eight symbols.
 */
#ifndef CPLUS_MATH_C23_FARITH_H
#define CPLUS_MATH_C23_FARITH_H

#include <float.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1 means the host's IEEE operators + one narrowing cast provably implement
 * the C23 contract: no excess precision (FLT_EVAL_METHOD == 0) and an
 * intermediate precision of at least 2*p_float+2 (innocuous double rounding).
 * Verified 1 on /usr/bin/clang 16.0.0 (clang-1600.0.26.6),
 * arm64-apple-darwin27.2.0: FLT_EVAL_METHOD=0, FLT_MANT_DIG=24,
 * DBL_MANT_DIG=53, LDBL_MANT_DIG=53. When 0, no symbols are emitted. */
#if defined(FLT_EVAL_METHOD) && FLT_EVAL_METHOD == 0 &&                       \
    defined(FLT_MANT_DIG) && defined(DBL_MANT_DIG) &&                         \
    defined(LDBL_MANT_DIG) &&                                                 \
    DBL_MANT_DIG >= 2 * FLT_MANT_DIG + 2 &&                                   \
    LDBL_MANT_DIG >= 2 * FLT_MANT_DIG + 2
#define CPLUS_C23_FARITH_HAVE_OPERATOR_BACKEND 1
#else
#define CPLUS_C23_FARITH_HAVE_OPERATOR_BACKEND 0
#endif

/* Inventory of this closure. */
#define CPLUS_C23_FARITH_CLOSED_COUNT 8
#define CPLUS_C23_FARITH_BLOCKED_COUNT 4

/* Machine-readable, single-sentence blockers for the four non-C23 names. */
#define CPLUS_C23_FARITH_BLOCKER_FADDF                                        \
    "faddf: not an ISO C23 API -- 7.12.14 defines only fadd(double,double) "   \
    "and faddl(long double,long double), both returning float; no host "      \
    "builtin or symbol, no pinned upstream file (faddf.cpp is 404); no "      \
    "contract to implement"
#define CPLUS_C23_FARITH_BLOCKER_FSUBF                                        \
    "fsubf: not an ISO C23 API -- 7.12.14 has no /f variant; no host "         \
    "builtin or symbol, no pinned upstream file (fsubf.cpp is 404); no "      \
    "contract to implement"
#define CPLUS_C23_FARITH_BLOCKER_FMULF                                        \
    "fmulf: not an ISO C23 API -- 7.12.14 has no /f variant; no host "         \
    "builtin or symbol, no pinned upstream file (fmulf.cpp is 404); no "      \
    "contract to implement"
#define CPLUS_C23_FARITH_BLOCKER_FDIVF                                        \
    "fdivf: not an ISO C23 API -- 7.12.14 has no /f variant; no host "         \
    "builtin or symbol, no pinned upstream file (fdivf.cpp is 404); no "      \
    "contract to implement"

#if CPLUS_C23_FARITH_HAVE_OPERATOR_BACKEND

float cplus_fadd(double x, double y);
float cplus_faddl(long double x, long double y);

float cplus_fsub(double x, double y);
float cplus_fsubl(long double x, long double y);

float cplus_fmul(double x, double y);
float cplus_fmull(long double x, long double y);

float cplus_fdiv(double x, double y);
float cplus_fdivl(long double x, long double y);

#endif /* CPLUS_C23_FARITH_HAVE_OPERATOR_BACKEND */

#ifdef __cplusplus
}
#endif
#endif /* CPLUS_MATH_C23_FARITH_H */
