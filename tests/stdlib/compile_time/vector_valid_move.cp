/* EXPECT: ACCEPT (positive control for ownership transfer).
 *
 * Reason: cplus_move(a) transfers the owned buffer to b and a is never used
 * again; only the new owner b is read and dropped.  c+ check --strict and
 * c+ build --strict --sanitize=none must both succeed with no [CPLUS-
 * diagnostic, and the program must exit 0.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

int main(void) {
    vector a = vector();
    a.push_back(12);
    a.push_back(13);

    vector b = cplus_move(a);
    int out = -1;
    int rc = b.at_checked(0, &out);
    print("valid-move", "size", b.size(), "rc", rc, "out", out);

    b.drop();
    return 0;
}
