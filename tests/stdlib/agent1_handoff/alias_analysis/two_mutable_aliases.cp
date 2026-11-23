/* P0 sound alias analysis — case 1: two mutable aliases to one stdlib object.
 * EXPECT: REJECT (P0 contract; currently ACCEPT — compiler capability gap).
 *
 * REQUESTED: REJECT with a tagged [CPLUS-ALIAS] diagnostic naming the two
 * overlapping mutable borrows ('left' and 'right') of owner 'v'.
 *
 * Both pointers are writable handles to the SAME owned vector; either can
 * reallocate/drop the shared buffer. Sound exclusivity analysis must forbid
 * holding both across a mutation. This case requests the compiler contract,
 * not a runtime refusal.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

int main(void) {
    vector v = vector();
    vector *left = &v;
    vector *right = &v;

    left->push_back(1);
    right->push_back(2);
    v.drop();
    return 0;
}
