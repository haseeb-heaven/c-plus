/* P0 sound alias analysis — case 5: alias through a function parameter
 * (interprocedural).
 * EXPECT: REJECT (P0 contract; currently ACCEPT — compiler capability gap).
 *
 * REQUESTED: REJECT with a tagged [CPLUS-ALIAS] diagnostic naming that 'a'
 * and 'b' are two mutable aliases of the same owner passed from main(), so
 * overlapping mutation through them is unsound.
 *
 * A sound analysis must propagate alias/exclusivity facts across the call
 * boundary; both writes go to one owned vector.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

static void mutate_pair(vector *a, vector *b) {
    a->push_back(1);
    b->push_back(2);
}

int main(void) {
    vector v = vector();
    mutate_pair(&v, &v); /* two aliases of one owner via parameters */
    v.drop();
    return 0;
}
