/* EXPECT: RUNTIME (requested REJECT; compiler cannot prove a container index).
 *
 * Reason: index 9 is a literal constant and the vector holds only two ints,
 * so this is a statically obvious out-of-bounds access.  A direct constant
 * subscript of a fixed-size C array IS rejected with
 * "[CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS] array index N exceeds length M", but
 * vector::at_checked() takes the index through an ordinary method parameter,
 * which C+ does not analyse statically.
 *
 * Exact observed outcome: c+ check --strict and c+ build --strict
 * --sanitize=none both SUCCEED with no [CPLUS- diagnostic.  At runtime
 * vector::at_checked() refuses the access (returns -1, leaves *out untouched)
 * and records a STDLIB_CATEGORY_BOUNDS violation.  This is a compile-time
 * capability gap, not C+ enforcement.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

int main(void) {
    vector v = vector();
    v.push_back(10);
    v.push_back(20);

    int out = -1;
    int rc = v.at_checked(9, &out); /* constant OOB: statically knowable */
    print("const-oob", rc, "size", v.size(), "out", out);

    v.drop();
    return 0;
}
