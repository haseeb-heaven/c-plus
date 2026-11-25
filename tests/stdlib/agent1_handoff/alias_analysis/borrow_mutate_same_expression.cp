/* P0 sound alias analysis — case 2: borrowed pointer + mutable method call in
 * the same full-expression.
 * EXPECT: REJECT (P0 contract; currently ACCEPT — compiler capability gap).
 *
 * REQUESTED: REJECT with a tagged [CPLUS-ALIAS] / [CPLUS-BORROW] diagnostic
 * naming the read through 'borrow' overlapping the mutating push_back on the
 * same owner in one expression.
 *
 * The argument evaluates through the borrow while the receiver mutates the
 * same object. Sound alias analysis must reject the overlapping uses; the
 * vector runtime has no such compile-time proof to offer.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

int main(void) {
    vector v = vector();
    v.push_back(7);

    vector *borrow = &v;
    borrow->push_back(borrow->at(0)); /* read via borrow + mutation, one expression */

    v.drop();
    return 0;
}
