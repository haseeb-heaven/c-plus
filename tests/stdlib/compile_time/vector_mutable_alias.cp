/* EXPECT: RUNTIME (requested REJECT; no alias/exclusivity analysis).
 *
 * Reason: `left` and `right` are two mutable aliases (vector *) of the same
 * owner v.  Both can push_back/erase/drop concurrently through their own
 * alias, which a container-aware borrow checker would forbid.  C+
 * explicitly implements only a targeted borrow subset (no aliasing or
 * exclusivity rules, no container-level tracking), so this compiles.
 *
 * Exact observed outcome: check and strict build SUCCEED with no [CPLUS-
 * diagnostic; the program runs cleanly because the checked methods keep the
 * shared buffer coherent (size 2).  Note: copying the owner itself
 * (`vector b = a;`) IS rejected at compile time as "cannot be copied" (also
 * untagged), but pointer aliasing is not.  This is a capability gap.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

int main(void) {
    vector v = vector();
    vector *left = &v;  /* mutable alias 1 */
    vector *right = &v; /* mutable alias 2 */

    left->push_back(1);
    right->push_back(2);
    left->push_back(3);
    right->erase(1, NULL);

    print("aliases", left->size(), right->size(), v.capacity());
    v.drop();
    return 0;
}
