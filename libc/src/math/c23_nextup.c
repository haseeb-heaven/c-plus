/* c23_nextup.c -- the six ISO C23 nextup/nextdown APIs, bare ISO spellings.
 *
 * Scope
 * -----
 * Adds the six C23 <math.h> entry points that docs/libc/iso_c_inventory.json
 * still classifies as MISSING (bare names, as opposed to the existing
 * cplus_nextup / cplus_nextdown wrappers in libc/src/math/math_c23_next.c):
 *
 *   nextup,   nextupf,   nextupl
 *   nextdown, nextdownf, nextdownl
 *
 * This translation unit defines SIX NEW global symbols with the exact ISO C
 * names and signatures. It renames, retypes and removes nothing, so the C ABI
 * of the existing library is unchanged. See docs/libc/C23_NEXTUP.md for the
 * nm before/after evidence.
 *
 * Provenance (no upstream source was copied)
 * ------------------------------------------
 * The pinned third_party/ checkout at this branch contains no nextup or
 * nextdown source: `grep -rn 'nextup\|nextdown' third_party/` produced no
 * matches. Consequently this is NOT a copy of musl, LLVM libc or any other
 * vendored implementation. The concept is derived from the ISO C23 standard
 * text for these functions (7.12.11.5 "The nextup functions" and 7.12.11.6
 * "The nextdown functions", inherited from WG14 TS 18661-1 / N1615 N1664
 * section 14.4), which specifies the next representable value in the type of
 * the function toward +infinity / -infinity, with these explicit cases:
 *
 *   nextup(+0)  = least positive subnormal;   nextup(-0)  = least positive
 *                                              subnormal (direction wins)
 *   nextup(least negative subnormal)          = -0
 *   nextdown(+0) = least negative subnormal;  nextdown(-0) = least negative
 *                                              subnormal (direction wins)
 *   nextdown(least positive subnormal)        = +0
 *   nextup(+inf) = +inf;  nextup(-inf) = -TYPE_MAX;
 *   nextdown(-inf) = -inf; nextdown(+inf) = +TYPE_MAX;
 *   nextup(TYPE_MAX) = +inf; nextdown(-TYPE_MAX) = -inf;
 *   NaN is returned unchanged (payload preserved).
 *
 * The implementation uses the nextafter-based definition requested for this
 * task, which realises exactly those cases on this host:
 *
 *   nextup(x)   = nextafter(x, +INFINITY)
 *   nextdown(x) = nextafter(x, -INFINITY)
 *
 * It is a thin PLATFORM_BACKED delegation to the C99 <math.h> nextafter
 * family (no builtin, no bit twiddling, no fabricated algorithm). Floating
 * point exception flags are therefore the host libm's, as documented in
 * docs/libc/C23_NEXTUP.md; this is a deviation from the "no exceptions"
 * recommendation for a dedicated C23 implementation, recorded openly.
 */
#include "../../install-include/cplus_c23_nextup.h"

#include <math.h>

float nextupf(float x) { return nextafterf(x, INFINITY); }

double nextup(double x) { return nextafter(x, INFINITY); }

long double nextupl(long double x) { return nextafterl(x, INFINITY); }

float nextdownf(float x) { return nextafterf(x, -INFINITY); }

double nextdown(double x) { return nextafter(x, -INFINITY); }

long double nextdownl(long double x) { return nextafterl(x, -INFINITY); }
