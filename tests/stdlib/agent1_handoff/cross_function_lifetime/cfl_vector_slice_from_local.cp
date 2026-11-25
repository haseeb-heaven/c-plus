// EXPECT: REJECT
// Cross-function lifetime (P0): make_slice() returns a vector::slice that
// borrows the storage of a local `vector`.  vector_drop at function exit
// releases the buffer and the slice's generation_ref points at the dead
// vector object, so the caller's slice dangles in two ways at once.
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

static slice make_slice(void) {
    vector v = vector();
    v.push_back(11);
    return v.view();           /* borrow of local v escapes the function */
}

int main(void) {
    slice s = make_slice();
    int out = -1;
    (void)s.at_checked(0, &out); /* dereferences the released buffer */
    return 0;
}
