/* cplus_c23_fenv_compat.h -- honest C23 <fenv.h> capability surface.
 *
 * Purpose
 * -------
 * C23 adds the rounding-mode macro FE_TIESAWAY to <fenv.h>. This header
 * publishes a *query* for that capability. It never fabricates a value for
 * the macro. On a host whose compiler/SDK does not define FE_TIESAWAY, the
 * query returns 0 and the interface is classified UNSUPPORTED-BY-HOST -- an
 * explicit, testable refusal -- rather than a silent MISSING gap or a
 * quietly invented rounding constant.
 *
 * Evidence (recorded 2026-11-23 on this tree; see docs/libc/C23_FENV_COMPAT.md)
 * ---------------------------------------------------------------------------
 *   $ printf '#include <fenv.h>\nint main(void) { return FE_TIESAWAY; }\n' \
 *       | /usr/bin/clang -x c -std=c11 -fsyntax-only - 2>&1
 *   <stdin>:2:25: error: use of undeclared identifier 'FE_TIESAWAY'
 *       2 | int main(void) { return FE_TIESAWAY; }
 *         |                         ^
 *   1 error generated.
 *   (clang exit status: 1)
 *
 *   /usr/bin/clang --version: Apple clang version 16.0.0 (clang-1600.0.26.6)
 *   Target: arm64-apple-darwin27.2.0
 *   SDK: /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk (MacOSX15.2)
 *
 *   <float.h>: no FE_* definition at all.
 *   <fenv.h>:  defines FE_TONEAREST/FE_UPWARD/FE_DOWNWARD/FE_TOWARDZERO
 *              (four modes) and no FE_TIESAWAY. An SDK-wide grep for
 *              FE_TIESAWAY under usr/include returns no file.
 *   third_party/: no FE_TIESAWAY definition.
 *
 * Policy -- why there is no fake constant
 * ---------------------------------------
 * FE_TIESAWAY is a compiler-provided macro, not a function or a data symbol.
 * There is no runtime symbol to bind and no numeric value to derive from the
 * host. The encodings of the four existing modes are host facts, but the
 * value of a fifth mode is a decision only the compiler can make; defining
 * one here would be false semantics (and could silently choose the wrong
 * FP control-register bits). Therefore:
 *   - no FE_TIESAWAY (or substitute) macro is emitted on this host;
 *   - cplus_fenv_has_tiesaway() returns 0 and is a real, link-free query;
 *   - the status is reported as UNSUPPORTED-BY-HOST via a machine-readable
 *     macro and a single authoritative blocker string.
 *
 * Forward compatibility
 * ---------------------
 * Detection consults the compiler header only. If a future host/SDK defines
 * FE_TIESAWAY, the capability flips automatically: the query returns 1,
 * CPLUS_FE_TIESAWAY aliases the REAL host macro, and the status becomes
 * SUPPORTED-BY-HOST. No fabricated value is ever introduced by this header.
 *
 * ABI: additive and header-only. One `static inline` query with internal
 * linkage; no public symbol, no new type, no object-file dependency. Safe to
 * include from C99/C11/C17/C2x and from C++ translation units.
 */
#ifndef CPLUS_C23_FENV_COMPAT_H
#define CPLUS_C23_FENV_COMPAT_H

#include <fenv.h>

/* --- Detection: the compiler header is the single source of truth -------- */
#if defined(FE_TIESAWAY)
#  define CPLUS_C23_FENV_HAVE_FE_TIESAWAY 1
#  define CPLUS_FE_TIESAWAY FE_TIESAWAY /* real macro; never synthesized */
#else
#  define CPLUS_C23_FENV_HAVE_FE_TIESAWAY 0
#endif

/* --- Machine-readable classification and single authoritative blocker ---- */
#if CPLUS_C23_FENV_HAVE_FE_TIESAWAY
#  define CPLUS_C23_FENV_TIESAWAY_STATUS "SUPPORTED-BY-HOST"
#  define CPLUS_C23_FENV_BLOCKER_FE_TIESAWAY                                  \
    "FE_TIESAWAY: SUPPORTED-BY-HOST; CPLUS_FE_TIESAWAY aliases the real "      \
    "<fenv.h> macro"
#else
#  define CPLUS_C23_FENV_TIESAWAY_STATUS "UNSUPPORTED-BY-HOST"
#  define CPLUS_C23_FENV_BLOCKER_FE_TIESAWAY                                  \
    "FE_TIESAWAY: UNSUPPORTED-BY-HOST; <fenv.h> defines only "                \
    "FE_TONEAREST/FE_UPWARD/FE_DOWNWARD/FE_TOWARDZERO (probe: use of "        \
    "undeclared identifier 'FE_TIESAWAY'); no numeric value fabricated"
#endif

/* --- The capability query ----------------------------------------------- */
static inline int cplus_fenv_has_tiesaway(void) {
    return CPLUS_C23_FENV_HAVE_FE_TIESAWAY;
}

#endif /* CPLUS_C23_FENV_COMPAT_H */
