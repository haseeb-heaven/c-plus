/* EXPECT: RUNTIME (requested REJECT; borrow staleness is dynamic).
 *
 * Reason: slice s borrows vector v's current buffer.  Pushing 8 more ints
 * forces reallocation and bumps v.generation, so s is stale and any read
 * through it must be rejected.  C+ does not connect a returned class value
 * (slice) to its source object, so it cannot prove staleness statically.
 *
 * Exact observed outcome: check and strict build SUCCEED with no [CPLUS-
 * diagnostic.  At runtime slice::stale() == 1, slice::valid() == 0 and
 * slice::at_checked(0, &out) returns -1 (out keeps its previous value).
 * The library catches the bug at runtime; the compiler does not.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

int main(void) {
    vector v = vector();
    v.push_back(1);
    v.push_back(2);

    slice s = v.view(); /* borrow of the current generation */
    int out = -1;
    int before = s.at_checked(0, &out);

    for (int i = 0; i < 8; i++)
        v.push_back(10 + i); /* growth invalidates s */

    int stale = s.stale();
    int valid = s.valid();
    int after = s.at_checked(0, &out); /* use of invalid slice */
    print("slice", before, "stale", stale, "valid", valid, "after", after);

    v.drop();
    return 0;
}
