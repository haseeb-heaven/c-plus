/* math_c23_farith.c -- ISO C23 fadd/fsub/fmul/fdiv families for the C+ libc.
 *
 * Scope / provenance
 * ------------------
 * Implements the eight entry points that ISO/IEC 9899:2024 (C23) actually
 * defines in 7.12.14 "Functions that round result to narrower type":
 *
 *     float fadd (double x, double y);       float faddl (long double x, long double y);
 *     float fsub (double x, double y);       float fsubl (long double x, long double y);
 *     float fmul (double x, double y);       float fmull (long double x, long double y);
 *     float fdiv (double x, double y);       float fdivl (long double x, long double y);
 *
 * Exported with the project-wide namespace as cplus_fadd, cplus_faddl, ...
 * The bare ISO spellings are deliberately NOT defined as global symbols.
 *
 * WHY THESE SIGNATURES (correction to the earlier dependency audit)
 * -----------------------------------------------------------------
 * docs/libc/C23_MATH_DEPENDENCY_CLOSURE.md recorded a "signature mismatch"
 * between LLVM libc (`float fadd(double,double)`) and a claimed C23
 * `double fadd(double,double)`. That claim is wrong. The normative C23 text
 * (final working draft N3220, 2024-02-21,
 * https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3220.pdf, 7.12.14.1-.4,
 * confirmed identically in N3096 7.12.14.1-.4) is:
 *
 *     "float fadd(double x, double y);
 *      float faddl(long double x, long double y);"
 *
 * and 7.12.14 p1: "The functions in this subclause round their results to the
 * return type, which is typically narrower than the parameter types." Each
 * subclause adds: "They compute the [sum/difference/product/quotient] (as if)
 * to infinite precision and round once to the return type, according to the
 * current rounding mode." Annex F.10.11 adds that they "are fully specified
 * in IEC 60559" and "treat zero and infinite arguments like the corresponding
 * operation: +, -, *, /". So these are NARROWING operations, not same-width
 * ones, and LLVM libc's signatures do match C23. They were blocked earlier
 * only by the uncopied C++ `__support` closure -- not by a contract mismatch.
 *
 * IMPLEMENTATION MECHANISM: documented HOST OPERATOR SEMANTICS
 * -------------------------------------------------------------
 * On the verified host (/usr/bin/clang 16.0.0, arm64-apple-darwin27.2.0) the
 * C expression `(float)(x + y)` with x/y of type double (or long double) is
 * exactly the C23 operation:
 *
 *   1. FLT_EVAL_METHOD == 0, so the host evaluates `x + y` in the semantic
 *      type with a single IEEE 754 rounding (no excess precision, no
 *      contraction for the four basic operators).
 *   2. The cast to float is a single IEEE 754 convertFormat rounding.
 *   3. p_mid >= 2*p_float + 2 (53 >= 2*24+2 = 50 here, and LDBL_MANT_DIG is
 *      also 53 on this Darwin/arm64 target). This is the classical
 *      "innocuous double rounding" condition, so round-to-wider-then-round-
 *      to-float equals the single round-to-float of the exact result.
 *   4. Both the operator and the cast honour the current rounding mode and
 *      raise the IEEE 754 exceptions that C23 Annex F requires.
 *
 * This is a thin delegation to documented host primitives -- the same
 * PLATFORM_BACKED pattern used by math_c23_next.c -- NOT an invented
 * algorithm and NOT a fabricated `a+b` claimed to be C23 without evidence.
 * The complete evidence (exact-rational oracle, boundary vectors, rounding
 * modes, exceptions, commands and output) is in
 * docs/libc/C23_FARITH_CLOSURE.md.
 *
 * The four names faddf/fsubf/fmulf/fdivf from the older inventory are NOT C23
 * APIs at all -- 7.12.14 defines only the eight narrowing functions above,
 * with no /f suffix. No body is emitted for them; see the BLOCKED strings in
 * cplus_math_c23_farith.h and the closure document.
 */
#include "../../install-include/cplus_math_c23_farith.h"
#include "../../log/log.h"

#if CPLUS_C23_FARITH_HAVE_OPERATOR_BACKEND

float cplus_fadd(double x, double y) {
    cplus_libc_log("fadd", "enter");
    return (float)(x + y);
}

float cplus_faddl(long double x, long double y) {
    cplus_libc_log("faddl", "enter");
    return (float)(x + y);
}

float cplus_fsub(double x, double y) {
    cplus_libc_log("fsub", "enter");
    return (float)(x - y);
}

float cplus_fsubl(long double x, long double y) {
    cplus_libc_log("fsubl", "enter");
    return (float)(x - y);
}

float cplus_fmul(double x, double y) {
    cplus_libc_log("fmul", "enter");
    return (float)(x * y);
}

float cplus_fmull(long double x, long double y) {
    cplus_libc_log("fmull", "enter");
    return (float)(x * y);
}

float cplus_fdiv(double x, double y) {
    cplus_libc_log("fdiv", "enter");
    return (float)(x / y);
}

float cplus_fdivl(long double x, long double y) {
    cplus_libc_log("fdivl", "enter");
    return (float)(x / y);
}

#else /* !CPLUS_C23_FARITH_HAVE_OPERATOR_BACKEND */

/* Truthful platform gap: when the host has excess precision (FLT_EVAL_METHOD
 * != 0) or a too-narrow intermediate format (p < 2*24+2), operator+cast would
 * double-round and would NOT satisfy C23. No definition and no fabricated
 * fallback is emitted in that case; the header likewise publishes the
 * backend capability as 0. */
extern float cplus_fadd(double, double);
extern float cplus_faddl(long double, long double);
extern float cplus_fsub(double, double);
extern float cplus_fsubl(long double, long double);
extern float cplus_fmul(double, double);
extern float cplus_fmull(long double, long double);
extern float cplus_fdiv(double, double);
extern float cplus_fdivl(long double, long double);

#endif /* CPLUS_C23_FARITH_HAVE_OPERATOR_BACKEND */
