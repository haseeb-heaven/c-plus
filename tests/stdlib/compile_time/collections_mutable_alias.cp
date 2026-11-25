/* EXPECT: RUNTIME (requested REJECT; no alias/exclusivity analysis).
 *
 * Reason: `left` and `right` are two mutable `intmap *` aliases of the same
 * owner `m`.  Either alias can insert, remove or drop the shared buffers,
 * which a container-aware borrow checker would forbid (exclusive access is
 * required for mutation).  C+ implements only a narrow borrow subset for
 * `resource struct` handles; it has no aliasing/exclusivity rules for
 * lightweight `class` owners, so this compiles.  Copying the owner itself
 * (`intmap b = a;`) IS rejected at compile time, but as an UNTAGGED
 * "resource 'b' cannot be copied from 'a'" message, so it does not count
 * either.
 *
 * Exact observed outcome: check and strict build both SUCCEED with no
 * [CPLUS- diagnostic; the checked methods keep the shared buffers coherent
 * (both aliases report the same size).  This is a capability gap.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/collections.hp"

int main(void) {
    intmap m = intmap();
    intmap *left = &m;  /* mutable alias 1 */
    intmap *right = &m; /* mutable alias 2 */

    left->put(1, 10);
    right->put(2, 20);
    right->remove(1);
    left->put(3, 30);

    print("aliases", left->size(), right->size(), m.size());
    m.drop();
    return 0;
}
