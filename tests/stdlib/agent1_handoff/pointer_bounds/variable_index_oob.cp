/* P0 pointer-bounds reproducer: VARIABLE-INDEX OOB where the index is
 * provably too large.
 *
 * idx is a const size_t initialised to 9 and never modified; the vector's
 * logical extent is 2, so idx >= extent is statically decidable.  The
 * compiler still accepts *(p + idx) because there is no extent metadata
 * linking the pointer p (v.items) to vector.used.
 *
 * The probe lives in a never-called function so the program contains no
 * executed UB; the check still sees the expression.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

static int pb_variable_index_oob(void) {
    vector v = vector();
    v.push_back(10);
    v.push_back(20);

    int *p = v.items;
    const size_t idx = 9;   /* provably >= used (2), never reassigned */
    int sink = *(p + idx);  /* OOB expression under test */

    v.drop();
    return sink;
}

int main(void) { return 0; }
