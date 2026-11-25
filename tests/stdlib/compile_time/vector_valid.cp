/* EXPECT: ACCEPT (positive control).
 *
 * Reason: every operation uses a live vector and the checked accessors with
 * in-range indices.  c+ check --strict and c+ build --strict --sanitize=none
 * must both succeed with no [CPLUS- diagnostic, and the program must exit 0.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

int main(void) {
    vector v = vector();
    v.push_back(7);
    v.push_back(8);
    v.push_back(9);

    int out = -1;
    int rc = v.at_checked(1, &out);
    print("valid", "size", v.size(), "rc", rc, "out", out, "at2", v.at(2));

    int last = -1;
    print("pop", v.pop_back(&last), "last", last, "size", v.size());
    v.drop();
    return 0;
}
