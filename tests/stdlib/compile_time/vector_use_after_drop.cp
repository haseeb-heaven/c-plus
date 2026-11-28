/* EXPECT: RUNTIME (requested REJECT; compiler cannot prove owner liveness).
 *
 * Reason: vector::drop() releases the backing buffer, so every later access
 * to v is a use-after-free of the owner's storage.  C+ tracks raw `managed`
 * pointers and resource structs, but it does not track the `int *items`
 * field inside a C+ class, so no [CPLUS-LIFETIME]/[CPLUS-OWNER] diagnostic
 * is produced here.
 *
 * Exact observed outcome: check and strict build both SUCCEED with no
 * [CPLUS- diagnostic.  The library's checked accessors report the dead state
 * at runtime (size() == 0 and at_checked(...) == -1).  A raw dereference of
 * the stale buffer is silent (no C+ check, reads recycled memory), so there
 * is no compile-time proof of safety.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

int main(void) {
    vector v = vector();
    v.push_back(11);
    v.push_back(22);
    v.drop(); /* releases items, zeroes used/space and bumps generation */

    int out = -1;
    int rc = v.at_checked(0, &out); /* use after drop */
    print("after-drop", "size", v.size(), "at_checked", rc, "out", out);
    print("compat-at", v.at(0));

    return 0;
}
