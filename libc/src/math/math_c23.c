/* math_c23.c -- ISO C23 <math.h> additions exposed on the C+ libc surface.
 *
 * Scope / provenance
 * ------------------
 * Integrates the 29 C23 math APIs recorded as MISSING for math.h in
 * docs/libc/iso_c_inventory.json:
 *   exp10/f/l, roundeven/f/l, nextup/f/l, nextdown/f/l,
 *   fadd/f/l, fsub/f/l, fmul/f/l, fdiv/f/l,
 *   the 3-argument hypot overload (exported here as cplus_hypot3),
 *   and the _Float16 helpers frexp16, fma16, logb16, exp16.
 *
 * Host probe (2026-11-22, /usr/bin/clang -std=c17, Apple Clang 16.0.0,
 * arm64-apple-darwin): a translation unit taking the address of each of the
 * 29 names fails for all 29 -- none is declared by the host <math.h>. A
 * second probe of __has_builtin(__builtin_<name>) shows that ONLY
 * roundeven/roundevenf/roundevenl have a host compiler builtin; those three
 * builtins were verified to compile, link with no unresolved libm symbol, and
 * return correct tie-to-even results. The vendored upstream subset
 * (libc/upstream/musl) contains no math sources, so there is no vendored
 * implementation to copy for the remaining 26 APIs.
 *
 * Policy (no algorithm is reimplemented and no fake result is returned):
 *   * When a compiler builtin exists, the cplus_* entry point is a thin
 *     wrapper that delegates to it and emits a cplus_libc_log marker.
 *   * Otherwise the ISO symbol is declared but deliberately left UNDEFINED,
 *     with a NOTES comment recording the exact platform gap. The matching
 *     tests in tests/libc/test_math_c23.c SKIP those entry points explicitly
 *     and report them in the skipped count.
 */
#include "../../log/log.h"
#include <math.h>

/* __has_builtin is a Clang/GCC extension; degrade to 0 on other compilers. */
#if defined(__has_builtin)
#define CPLUS_C23_HAS_BUILTIN(x) __has_builtin(x)
#else
#define CPLUS_C23_HAS_BUILTIN(x) 0
#endif

/* ==================================================================== */
/* exp10 family                                                          */
/* ==================================================================== */
#if CPLUS_C23_HAS_BUILTIN(__builtin_exp10)
double cplus_exp10(double x) {
    cplus_libc_log("exp10", "enter");
    return __builtin_exp10(x);
}
#else
/* NOTES: platform gap. Apple Clang 16 / macOS libSystem provides neither an
 * exp10 symbol nor __builtin_exp10, and the vendored musl subset has no
 * exp10 source. Declaration kept for the public surface; no definition is
 * emitted (an emulated exp10 would be an invented algorithm). */
extern double cplus_exp10(double);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_exp10f)
float cplus_exp10f(float x) {
    cplus_libc_log("exp10f", "enter");
    return __builtin_exp10f(x);
}
#else
/* NOTES: platform gap (host has no exp10f symbol or __builtin_exp10f; no
 * vendored upstream implementation). Declared only, intentionally undefined. */
extern float cplus_exp10f(float);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_exp10l)
long double cplus_exp10l(long double x) {
    cplus_libc_log("exp10l", "enter");
    return __builtin_exp10l(x);
}
#else
/* NOTES: platform gap (host has no exp10l symbol or __builtin_exp10l; no
 * vendored upstream implementation). Declared only, intentionally undefined. */
extern long double cplus_exp10l(long double);
#endif

/* ==================================================================== */
/* roundeven family -- the only C23 family with a host compiler builtin.  */
/* ==================================================================== */
#if CPLUS_C23_HAS_BUILTIN(__builtin_roundeven)
double cplus_roundeven(double x) {
    cplus_libc_log("roundeven", "enter");
    return __builtin_roundeven(x);
}
#else
/* NOTES: platform gap. No host roundeven symbol and no
 * __builtin_roundeven on this toolchain; declared only, undefined. */
extern double cplus_roundeven(double);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_roundevenf)
float cplus_roundevenf(float x) {
    cplus_libc_log("roundevenf", "enter");
    return __builtin_roundevenf(x);
}
#else
/* NOTES: platform gap. No host roundevenf symbol and no
 * __builtin_roundevenf on this toolchain; declared only, undefined. */
extern float cplus_roundevenf(float);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_roundevenl)
long double cplus_roundevenl(long double x) {
    cplus_libc_log("roundevenl", "enter");
    return __builtin_roundevenl(x);
}
#else
/* NOTES: platform gap. No host roundevenl symbol and no
 * __builtin_roundevenl on this toolchain; declared only, undefined. */
extern long double cplus_roundevenl(long double);
#endif

/* ==================================================================== */
/* nextup / nextdown families                                            */
/* ==================================================================== */
/* NOTES: platform gap. The host <math.h> declares none of nextup/nextupf/
 * nextupl/nextdown/nextdownf/nextdownl, __has_builtin is false for all six,
 * and the vendored musl subset ships no implementation. The ISO symbols are
 * declared below for surface completeness but are intentionally left
 * undefined; tests SKIP them rather than asserting fabricated results. */
#if CPLUS_C23_HAS_BUILTIN(__builtin_nextup)
double cplus_nextup(double x) {
    cplus_libc_log("nextup", "enter");
    return __builtin_nextup(x);
}
#else
extern double cplus_nextup(double);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_nextupf)
float cplus_nextupf(float x) {
    cplus_libc_log("nextupf", "enter");
    return __builtin_nextupf(x);
}
#else
extern float cplus_nextupf(float);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_nextupl)
long double cplus_nextupl(long double x) {
    cplus_libc_log("nextupl", "enter");
    return __builtin_nextupl(x);
}
#else
extern long double cplus_nextupl(long double);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_nextdown)
double cplus_nextdown(double x) {
    cplus_libc_log("nextdown", "enter");
    return __builtin_nextdown(x);
}
#else
extern double cplus_nextdown(double);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_nextdownf)
float cplus_nextdownf(float x) {
    cplus_libc_log("nextdownf", "enter");
    return __builtin_nextdownf(x);
}
#else
extern float cplus_nextdownf(float);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_nextdownl)
long double cplus_nextdownl(long double x) {
    cplus_libc_log("nextdownl", "enter");
    return __builtin_nextdownl(x);
}
#else
extern long double cplus_nextdownl(long double);
#endif

/* ==================================================================== */
/* fadd / fsub / fmul / fdiv families                                    */
/* ==================================================================== */
/* NOTES: platform gap. The 12 C23 correctly-rounded basic-operation entry
 * points (fadd/faddf/faddl, fsub/fsubf/fsubl, fmul/fmulf/fmull,
 * fdiv/fdivf/fdivl) are absent from the host header, have no compiler
 * builtin, and are not present in the vendored musl subset. They are declared
 * below but intentionally undefined: implementing them would require
 * duplicating an algorithm, which this file expressly does not do. */
#if CPLUS_C23_HAS_BUILTIN(__builtin_fadd)
double cplus_fadd(double x, double y) {
    cplus_libc_log("fadd", "enter");
    return __builtin_fadd(x, y);
}
#else
extern double cplus_fadd(double, double);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_faddf)
float cplus_faddf(float x, float y) {
    cplus_libc_log("faddf", "enter");
    return __builtin_faddf(x, y);
}
#else
extern float cplus_faddf(float, float);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_faddl)
long double cplus_faddl(long double x, long double y) {
    cplus_libc_log("faddl", "enter");
    return __builtin_faddl(x, y);
}
#else
extern long double cplus_faddl(long double, long double);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_fsub)
double cplus_fsub(double x, double y) {
    cplus_libc_log("fsub", "enter");
    return __builtin_fsub(x, y);
}
#else
extern double cplus_fsub(double, double);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_fsubf)
float cplus_fsubf(float x, float y) {
    cplus_libc_log("fsubf", "enter");
    return __builtin_fsubf(x, y);
}
#else
extern float cplus_fsubf(float, float);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_fsubl)
long double cplus_fsubl(long double x, long double y) {
    cplus_libc_log("fsubl", "enter");
    return __builtin_fsubl(x, y);
}
#else
extern long double cplus_fsubl(long double, long double);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_fmul)
double cplus_fmul(double x, double y) {
    cplus_libc_log("fmul", "enter");
    return __builtin_fmul(x, y);
}
#else
extern double cplus_fmul(double, double);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_fmulf)
float cplus_fmulf(float x, float y) {
    cplus_libc_log("fmulf", "enter");
    return __builtin_fmulf(x, y);
}
#else
extern float cplus_fmulf(float, float);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_fmull)
long double cplus_fmull(long double x, long double y) {
    cplus_libc_log("fmull", "enter");
    return __builtin_fmull(x, y);
}
#else
extern long double cplus_fmull(long double, long double);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_fdiv)
double cplus_fdiv(double x, double y) {
    cplus_libc_log("fdiv", "enter");
    return __builtin_fdiv(x, y);
}
#else
extern double cplus_fdiv(double, double);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_fdivf)
float cplus_fdivf(float x, float y) {
    cplus_libc_log("fdivf", "enter");
    return __builtin_fdivf(x, y);
}
#else
extern float cplus_fdivf(float, float);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_fdivl)
long double cplus_fdivl(long double x, long double y) {
    cplus_libc_log("fdivl", "enter");
    return __builtin_fdivl(x, y);
}
#else
extern long double cplus_fdivl(long double, long double);
#endif

/* ==================================================================== */
/* 3-argument hypot (C23 overload)                                       */
/* ==================================================================== */
/* NOTES: platform gap. The host hypot is the C99 two-argument function
 * (verified: a three-argument call is rejected under both -std=c17 and
 * -std=c2x), there is no __builtin_hypot3, and the vendored musl subset has
 * no hypot source. Exported under the distinct name cplus_hypot3 to avoid
 * colliding with the existing two-argument cplus_hypot. Declared only: a
 * hand-composed sqrt(x*x+y*y+z*z) would overflow/round differently and is an
 * invented algorithm, so it is deliberately not provided here. */
#if CPLUS_C23_HAS_BUILTIN(__builtin_hypot3)
double cplus_hypot3(double x, double y, double z) {
    cplus_libc_log("hypot3", "enter");
    return __builtin_hypot3(x, y, z);
}
#else
extern double cplus_hypot3(double, double, double);
#endif

/* ==================================================================== */
/* _Float16 helpers (C23)                                                */
/* ==================================================================== */
/* The four _Float16 entry points are only declared when the compiler
 * provides the _Float16 type (Apple Clang 16 does: __FLT16_MANT_DIG__ == 11).
 * On this host they remain a platform gap: no host symbol, no builtin, and no
 * vendored upstream source. They are declared but intentionally undefined.
 */
#if defined(__FLT16_MANT_DIG__)
#if CPLUS_C23_HAS_BUILTIN(__builtin_frexp16)
_Float16 cplus_frexp16(_Float16 x, int *exp) {
    cplus_libc_log("frexp16", "enter");
    return __builtin_frexp16(x, exp);
}
#else
/* NOTES: platform gap -- no frexp16 symbol, builtin or vendored source. */
extern _Float16 cplus_frexp16(_Float16, int *);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_fma16)
_Float16 cplus_fma16(_Float16 x, _Float16 y, _Float16 z) {
    cplus_libc_log("fma16", "enter");
    return __builtin_fma16(x, y, z);
}
#else
/* NOTES: platform gap -- no fma16 symbol, builtin or vendored source. */
extern _Float16 cplus_fma16(_Float16, _Float16, _Float16);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_logb16)
_Float16 cplus_logb16(_Float16 x) {
    cplus_libc_log("logb16", "enter");
    return __builtin_logb16(x);
}
#else
/* NOTES: platform gap -- no logb16 symbol, builtin or vendored source. */
extern _Float16 cplus_logb16(_Float16);
#endif

#if CPLUS_C23_HAS_BUILTIN(__builtin_exp16)
_Float16 cplus_exp16(_Float16 x) {
    cplus_libc_log("exp16", "enter");
    return __builtin_exp16(x);
}
#else
/* NOTES: platform gap -- no exp16 symbol, builtin or vendored source. */
extern _Float16 cplus_exp16(_Float16);
#endif
#endif /* __FLT16_MANT_DIG__ */
