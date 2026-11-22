/* EXPECT: ACCEPT (positive control for borrowed slices).
 *
 * Reason: the slice (and its sub-slice) is consumed while the owning vector
 * is unchanged, so it is neither stale nor empty.  c+ check --strict and
 * c+ build --strict --sanitize=none must both succeed with no [CPLUS-
 * diagnostic, and the program must exit 0.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

int main(void) {
    vector v = vector();
    v.push_back(4);
    v.push_back(5);
    v.push_back(6);

    slice s = v.view();
    slice part = s.sub(1, 2); /* [5, 6) - still the same generation */
    int out = -1;
    int rc = part.at_checked(1, &out);
    print("valid-slice", "size", s.size(), "sub", part.size(), "stale",
          part.stale(), "rc", rc, "out", out);

    v.drop();
    return 0;
}
