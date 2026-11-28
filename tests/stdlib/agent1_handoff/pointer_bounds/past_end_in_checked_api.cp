/* P0 pointer-bounds reproducer: POINTER PAST END used in an at_checked-like
 * API.
 *
 * A slice is a borrowed window over a stdlib-owned buffer with its own
 * at_checked(index, out) accessor.  Here the slice's base pointer is
 * v.items + v.size(), i.e. exactly one-past-the-end of the live elements.
 * at_checked(0, &out) then dereferences that past-the-end address.  The
 * library's runtime check only compares 0 against the (incorrect) slice
 * extent 1, so it cannot see that the base pointer itself is out of range.
 *
 * The probe lives in a never-called function so the program contains no
 * executed UB; the check still sees the expression.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

static int pb_past_end_checked_api(void) {
    vector v = vector();
    v.push_back(10);
    v.push_back(20);

    /* base pointer is one-past-end of the 2 live ints */
    slice s = slice(v.items + v.size(), 1);
    int out = -1;
    int rc = s.at_checked(0, &out);  /* OOB expression under test */

    v.drop();
    return rc;
}

int main(void) { return 0; }
