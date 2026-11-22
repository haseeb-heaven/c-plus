/* math_c23_upstream.c -- audit record for copying C23 <math.h> APIs from
 * pinned upstream LLVM libc source.
 *
 * PROVENANCE (all URLs fetched 2026-11-23):
 *   Project : llvm/llvm-project/libc
 *   Tag     : llvmorg-20.1.1
 *   License : Apache-2.0 WITH LLVM-exception
 *             (SPDX header of every file below; https://llvm.org/LICENSE.txt)
 *   Base    : https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-20.1.1/libc/
 *
 * AUDIT RESULT
 * ------------
 * 0 of the 23 MISSING C23 math APIs in docs/libc/iso_c_inventory.json could be
 * copied under the mandated rule: "ONLY copy it if its dependency closure is
 * self-contained or depends solely on libc functions we already provide;
 * if it needs LLVM libc C++ internals (src/__support/...), DO NOT copy."
 *
 * Every upstream implementation for these 23 APIs lives in
 * `libc/src/math/generic/<name>.cpp` and is a one-line wrapper around a
 * template in `libc/src/__support/FPUtil/` (FPBits, DyadicFloat, NormalFloat,
 * ExceptValues, quick_mul, ...). Those templates transitively include the
 * whole LLVM libc C++ support stack (src/__support/CPP, hdr, macros),
 * so no dependency closure here is self-contained, and none of the called
 * helpers is a libc function this project already exposes as cplus_*.
 *
 * Consequently this translation unit deliberately DEFINES NO API. It mirrors
 * the public surface with extern declarations only (matching the policy used
 * by math_c23.c) and records the exact blocker per API. No stub, no fake
 * result and no reimplemented algorithm is emitted. Tests SKIP all 23 and
 * report them explicitly; see tests/libc/test_math_c23_upstream.c and
 * docs/libc/C23_MATH_UPSTREAM.md for the full per-API table and exact URLs.
 */

#include "../../log/log.h" /* cplus_libc_log / cplus_libc_log_set_enabled (referenced by tests) */

/* ------------------------------------------------------------------------ */
/* nextup / nextdown family (6)                                             */
/* ------------------------------------------------------------------------ */
/* Upstream: libc/src/math/generic/{nextup,nextupf,nextupl,nextdown,
 * nextdownf,nextdownl}.cpp -- each is:
 *   return fputil::nextupdown<IsDown=...>(x);  // template bool in upstream
 * Blocker: `fputil::nextupdown` is a C++ template in
 *   src/__support/FPUtil/ManipulationFunctions.h, which includes FPBits.h,
 *   NormalFloat.h, dyadic_float.h, cast.h, rounding_mode.h, FEnvImpl.h,
 *   src/__support/CPP/{bit,limits,type_traits}.h, hdr/math_macros.h and
 *   macros. Its bit-level core (FPBits<T>::uintval/min_subnormal/max_normal)
 *   has no cplus_* equivalent here. Not copied. */
extern double cplus_nextup(double);
extern float cplus_nextupf(float);
extern long double cplus_nextupl(long double);
extern double cplus_nextdown(double);
extern float cplus_nextdownf(float);
extern long double cplus_nextdownl(long double);

/* ------------------------------------------------------------------------ */
/* fadd / fsub / fmul / fdiv family (12)                                    */
/* ------------------------------------------------------------------------ */
/* Upstream: libc/src/math/generic/{fadd,faddl,fsub,fsubl,fmulf,fmull,
 * fdiv,fdivl}.cpp -> fputil::generic::{add,sub,mul,div}<float>(x, y) from
 *   src/__support/FPUtil/generic/{add_sub,mul,div}.h
 *   (DyadicFloat, cpp::bit_ceil, quick_mul, FPBits, ...).
 *   fmul.cpp additionally uses src/__support/FPUtil/double_double.h
 *   (fputil::exact_mult, fputil::DoubleDouble), Sign, hdr/errno_macros.h and
 *   hdr/fenv_macros.h.
 * Two independent blockers:
 *   (1) all helpers are src/__support C++ templates -- not self-contained,
 *       no cplus_* equivalent;
 *   (2) SIGNATURE MISMATCH: upstream declares `float fadd(double,double)`
 *       (see src/math/fadd.h), `float faddl(long double,long double)`, etc.
 *       C23 requires `double fadd(double,double)` and
 *       `double faddl(long double,long double)`. Copying would not implement
 *       the C23 API.
 * Also absent upstream (confirmed HTTP 404 at this tag): faddf.cpp,
 * fsubf.cpp, fmulf.cpp and fdivf.cpp -- there is no single-precision
 * entry point to copy for the /f variants at all.
 * Not copied. */
extern double cplus_fadd(double, double);
extern float cplus_faddf(float, float);
extern long double cplus_faddl(long double, long double);
extern double cplus_fsub(double, double);
extern float cplus_fsubf(float, float);
extern long double cplus_fsubl(long double, long double);
extern double cplus_fmul(double, double);
extern float cplus_fmulf(float, float);
extern long double cplus_fmull(long double, long double);
extern double cplus_fdiv(double, double);
extern float cplus_fdivf(float, float);
extern long double cplus_fdivl(long double, long double);

/* ------------------------------------------------------------------------ */
/* 3-argument hypot (1)                                                     */
/* ------------------------------------------------------------------------ */
/* Upstream at this tag has ONLY the 2-argument hypot:
 *   libc/src/math/generic/hypot.cpp -> fputil::hypot(x, y) from
 *   src/__support/FPUtil/Hypot.h; src/math/hypot.h declares hypot(double,double).
 * Blocker (1): fputil::hypot is a C++ __support template (FPBits, ...).
 * Blocker (2): no 3-argument overload exists anywhere in the pinned tree
 *   (generic/hypot3.cpp and generic/hypotf16.cpp are HTTP 404). There is no
 *   upstream source to copy for the C23 hypot(x, y, z) API.
 * Exported under the distinct name cplus_hypot3 to avoid colliding with the
 * existing two-argument cplus_hypot. Not copied. */
extern double cplus_hypot3(double, double, double);

/* ------------------------------------------------------------------------ */
/* _Float16 helpers: frexp16, fma16, logb16, exp16 (4)                      */
/* ------------------------------------------------------------------------ */
/* LLVM libc names these with an `f16` suffix (C23 name -> upstream file):
 *   frexp16 -> generic/frexpf16.cpp -> fputil::frexp(x, *exp)
 *   logb16  -> generic/logbf16.cpp  -> fputil::logb(x)
 *   exp16   -> generic/expf16.cpp   -> full minimax algorithm using
 *              fputil::FPBits<float16>, fputil::ExceptValues, fputil::polyeval,
 *              fputil::cast, fputil::quick_get_round and exp_range_reduction()
 *              from generic/expxf16.h (large exception/constant tables).
 *   fma16   -> generic/fmaf16.cpp is HTTP 404 at this tag; the closest file
 *              is generic/f16fma.cpp, which implements
 *              `float16 f16fma(double,double,double)` via
 *              `fputil::fma<float16>` (src/__support/FPUtil/FMA.h). That is
 *              both a __support dependency AND a signature mismatch with
 *              C23 `_Float16 fma16(_Float16,_Float16,_Float16)`.
 * Blocker for all four: fputil::{frexp,logb,fma} and the expf16 algorithm are
 * src/__support/FPUtil C++ machinery; exp16 additionally needs two upstream
 * tables from expxf16.h. No self-contained closure, no cplus_* substitute.
 * Not copied. The declarations below are guarded exactly as in math_c23.c. */
#if defined(__FLT16_MANT_DIG__)
extern _Float16 cplus_frexp16(_Float16, int *);
extern _Float16 cplus_fma16(_Float16, _Float16, _Float16);
extern _Float16 cplus_logb16(_Float16);
extern _Float16 cplus_exp16(_Float16);
#endif /* __FLT16_MANT_DIG__ */
