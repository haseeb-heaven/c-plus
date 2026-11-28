/* KNOWN_OPEN negative fixture -- residual gap of issue #4 (cross-TU
 * declarations); see docs/LIMITATIONS.md Bug 35 and docs/compiler/abi_multi.md.
 *
 * Translation unit A: the OWNER side.
 *
 * Defines the ownership-release boundary for a shared resource with the
 * signature `void resource_release(int *)`.  The parameter is an owned managed
 * allocation and this function releases it exactly once via `cplus_release`.
 * `resource_acquire` produces such an owned pointer through the C+ runtime.
 *
 * TU B (cross_tu_owner_decl.c) declares the SAME external symbol with an
 * incompatible pointer/ownership signature (`void resource_release(long *)`).
 * The C+ compiler analyses one TU at a time, so it never compares the two
 * declarations.  This fixture is expected to compile, link and run with NO
 * mismatch diagnostic -- evidence of the documented KNOWN_OPEN limitation.
 *
 * The plain `int *` / `long *` C pointer representation is unchanged; this
 * fixture introduces no new ABI, wrapper type or provenance token.
 */
#include <cplus/cplus.h>

void resource_release(int *p) { cplus_release(p); }

int *resource_acquire(void) { return (int *)cplus_alloc(sizeof(int)); }
