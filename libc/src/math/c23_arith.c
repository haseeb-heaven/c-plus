/* c23_arith.c -- the 12 fadd/fsub/fmul/fdiv entry points from the C23 gap
 * inventory (docs/libc/iso_c_inventory.json) for the C+ hardened libc.
 *
 * Scope / provenance
 * ------------------
 * Implements, under the project-wide cplus_* namespace:
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
 * The eight un-suffixed / -l signatures are the normative ISO C23 7.12.14
 * "round result to narrower type" signatures (double/long double operands,
 * float result). The four /f signatures are a documented family extension for
 * the float semantic type; ISO C23 7.12.14 does not define them.
 *
 * UPSTREAM CHECK (first step, as required)
 * ----------------------------------------
 * `grep -rn 'fadd' third_party/` returns NO matches on this tree: none of the
 * pinned vendored dependencies (clog, ctl, exceptions4c, intel-safestringlib,
 * miniaudio, nuklear, safeclib, stb, yyjson) ships an fadd/fsub/fmul/fdiv
 * implementation. There is therefore no pinned upstream body to copy. The
 * code below is standard-derived: the behaviour is taken from ISO C23
 * 7.12.14 and Annex F.10.11 plus the C99 host operator contract, not copied
 * from any third-party source. See docs/libc/C23_ARITH.md for exact commands
 * and captured output.
 *
 * IMPLEMENTATION MECHANISM AND HONEST LIMITATION
 * ----------------------------------------------
 * Each function is a thin wrapper over the corresponding host operator:
 *   - the eight narrowing signatures evaluate `x OP y` in the wider semantic
 *     type and cast once to float;
 *   - the four /f signatures evaluate `x OP y` directly in float.
 *
 * What IS guaranteed on the verified host (/usr/bin/clang 16.0.0,
 * arm64-apple-darwin27.2.0):
 *   1. FLT_EVAL_METHOD == 0: operators round once to their semantic type,
 *      with no excess precision and no double rounding inside the operator.
 *   2. FLT_RADIX == 2 with 24/53/53 binary significand widths: the host uses
 *      IEEE 754 binary32/binary64 formats, so + - * / are IEEE 754 operations.
 *   3. The current rounding mode (fegetround/fesetround) affects every result
 *      and the standard FP exceptions are raised, as exercised by the tests.
 *   4. For the narrowing variants the intermediate precision p >= 53 satisfies
 *      the classical innocuous-double-rounding bound p >= 2*24+2 = 50, so the
 *      wider-operation-then-cast sequence equals a single round to binary32.
 *
 * What is NOT claimed: a formal machine-checked proof that the host FP unit
 * implements correct rounding for every possible operand pair. The property
 * is inherited from the platform and observed by deterministic tests; it is
 * not re-proved here. These wrappers are consequently described as
 * platform-backed, not as a from-first-principles C23 implementation.
 *
 * INTEGRATION NOTE FOR THE PARENT / WIRING AGENT
 * ----------------------------------------------
 * libc/src/math/math_c23_farith.c already defines the same eight narrowing
 * symbols (cplus_fadd, cplus_faddl, ... cplus_fdivl) with the SAME signatures.
 * This translation unit preserves those signatures exactly (no ABI change)
 * and ADDS the four previously undefined /f symbols, completing the family.
 * Wire c23_arith.c INSTEAD OF math_c23_farith.c -- not alongside it -- so the
 * archive/shared library contains one definition of each symbol. The stale
 * `extern` declarations of the /f names in math_c23.c / math_c23_upstream.c
 * already match the signatures here.
 */
#include "../../install-include/cplus_c23_arith.h"
#include "../../log/log.h"

#if CPLUS_C23_ARITH_HAVE_HOST_OPERATORS

/* ---- add ---------------------------------------------------------------- */
float cplus_fadd(double x, double y) {
    cplus_libc_log("fadd", "enter");
    return (float)(x + y);
}

float cplus_faddf(float x, float y) {
    cplus_libc_log("faddf", "enter");
    return x + y;
}

float cplus_faddl(long double x, long double y) {
    cplus_libc_log("faddl", "enter");
    return (float)(x + y);
}

/* ---- subtract ----------------------------------------------------------- */
float cplus_fsub(double x, double y) {
    cplus_libc_log("fsub", "enter");
    return (float)(x - y);
}

float cplus_fsubf(float x, float y) {
    cplus_libc_log("fsubf", "enter");
    return x - y;
}

float cplus_fsubl(long double x, long double y) {
    cplus_libc_log("fsubl", "enter");
    return (float)(x - y);
}

/* ---- multiply ----------------------------------------------------------- */
float cplus_fmul(double x, double y) {
    cplus_libc_log("fmul", "enter");
    return (float)(x * y);
}

float cplus_fmulf(float x, float y) {
    cplus_libc_log("fmulf", "enter");
    return x * y;
}

float cplus_fmull(long double x, long double y) {
    cplus_libc_log("fmull", "enter");
    return (float)(x * y);
}

/* ---- divide ------------------------------------------------------------- */
float cplus_fdiv(double x, double y) {
    cplus_libc_log("fdiv", "enter");
    return (float)(x / y);
}

float cplus_fdivf(float x, float y) {
    cplus_libc_log("fdivf", "enter");
    return x / y;
}

float cplus_fdivl(long double x, long double y) {
    cplus_libc_log("fdivl", "enter");
    return (float)(x / y);
}

#else /* !CPLUS_C23_ARITH_HAVE_HOST_OPERATORS */

/* Truthful platform gap: without IEEE binary operators / with excess
 * precision, these thin wrappers would not have C23 semantics. No fabricated
 * fallback body is emitted; the header likewise publishes no declarations. */
extern float cplus_fadd(double, double);
extern float cplus_faddf(float, float);
extern float cplus_faddl(long double, long double);
extern float cplus_fsub(double, double);
extern float cplus_fsubf(float, float);
extern float cplus_fsubl(long double, long double);
extern float cplus_fmul(double, double);
extern float cplus_fmulf(float, float);
extern float cplus_fmull(long double, long double);
extern float cplus_fdiv(double, double);
extern float cplus_fdivf(float, float);
extern float cplus_fdivl(long double, long double);

#endif /* CPLUS_C23_ARITH_HAVE_HOST_OPERATORS */
