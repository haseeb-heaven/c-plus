/* P0 pointer-bounds reproducer: INTERIOR POINTER WITH WRONG EXTENT.
 *
 * The slice is built from an interior pointer (v.items + 1) but claims an
 * extent of 4 ints, while only 1 live int remains from that position.
 * at_checked(3, &out) passes the library's 0 <= index < count test yet reads
 * v.items[4], past the owned buffer's live extent.  A compiler that attached
 * extent metadata to the buffer would see base + 3 exceeds the source
 * extent; C+ currently does not.
 *
 * The probe lives in a never-called function so the program contains no
 * executed UB; the check still sees the expression.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

static int pb_interior_wrong_extent(void) {
    vector v = vector();
    v.push_back(10);
    v.push_back(20);

    /* interior pointer +1, but extent 4 while only 1 int remains */
    slice s = slice(v.items + 1, 4);
    int out = -1;
    int rc = s.at_checked(3, &out);  /* OOB expression under test */

    v.drop();
    return rc;
}

int main(void) { return 0; }
