/* math_c23_next.c -- ISO C23 nextup/nextdown families for the C+ libc surface.
 *
 * Scope / provenance
 * ------------------
 * Implements the six C23 <math.h> nextup/nextdown entry points that math_c23.c
 * and math_c23_upstream.c previously left declared-but-undefined as a platform
 * gap:
 *   nextup / nextupf / nextupl, nextdown / nextdownf / nextdownl
 *
 * They are exported with the project-wide cplus_* namespacing convention:
 *   cplus_nextup  cplus_nextupf  cplus_nextupl
 *   cplus_nextdown cplus_nextdownf cplus_nextdownl
 * The bare ISO spellings (nextup, nextdown, ...) are deliberately NOT defined
 * as global symbols here; see docs/libc/C23_NEXT_CLOSURE.md for the rationale.
 *
 * Host probe (2026-11-23, /usr/bin/clang 16.0.0, arm64-apple-darwin27.2.0):
 *   * __has_builtin(__builtin_nextafter) == 1, while
 *     __has_builtin(__builtin_nextup) == __has_builtin(__builtin_nextdown) == 0.
 *   * The C17 <math.h> declares nextafter/nextafterf/nextafterl and they link
 *     against the host libm with no unresolved symbol.
 *   * A bit-level differential of nextafter(x, +INFINITY) / nextafter(x,
 *     -INFINITY) against the ISO C23 requirements matched in every tested
 *     case: NaN payload is preserved with no FE_INVALID; +/-0 map to the least
 *     subnormal of the appropriate sign; the +/-infinity and +/-TYPE_MAX
 *     boundaries behave as C23 prescribes. See docs/libc/C23_NEXT_CLOSURE.md
 *     for the exact commands and captured output.
 *
 * Policy (no fabricated algorithm, no invented bit twiddling):
 *   nextup(x)   == nextafter(x, +INFINITY)
 *   nextdown(x) == nextafter(x, -INFINITY)
 * This is the canonical host-backed definition of the two C23 operations.
 * It is a thin delegation to the host libm nextafter family (a PLATFORM_BACKED
 * wrapper, in this project's terminology), not a from-scratch implementation.
 * Any residual behavioral difference from a dedicated C23 implementation is
 * recorded in docs/libc/C23_NEXT_CLOSURE.md rather than hidden here.
 */
#include "../../log/log.h"
#include <math.h>

double cplus_nextup(double x) {
    cplus_libc_log("nextup", "enter");
    return nextafter(x, INFINITY);
}

float cplus_nextupf(float x) {
    cplus_libc_log("nextupf", "enter");
    return nextafterf(x, INFINITY);
}

long double cplus_nextupl(long double x) {
    cplus_libc_log("nextupl", "enter");
    return nextafterl(x, INFINITY);
}

double cplus_nextdown(double x) {
    cplus_libc_log("nextdown", "enter");
    return nextafter(x, -INFINITY);
}

float cplus_nextdownf(float x) {
    cplus_libc_log("nextdownf", "enter");
    return nextafterf(x, -INFINITY);
}

long double cplus_nextdownl(long double x) {
    cplus_libc_log("nextdownl", "enter");
    return nextafterl(x, -INFINITY);
}
