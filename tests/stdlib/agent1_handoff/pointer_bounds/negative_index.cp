/* P0 pointer-bounds reproducer: NEGATIVE INDEX into a stdlib-owned buffer.
 *
 * p points at the start of v.items (a stdlib_alloc buffer).  p - 1 / p[-1]
 * addresses one int before the allocation.  size_t indices wrap (the
 * at_checked spelling (size_t)-1 becomes a huge unsigned index), but the
 * pointer-arithmetic spelling is an underflow that a bounds pass must catch
 * by keeping signed offsets in the extent comparison.
 *
 * The probe lives in a never-called function so the program contains no
 * executed UB; the check still sees the expression.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

static int pb_negative_index(void) {
    vector v = vector();
    v.push_back(10);
    v.push_back(20);

    int *p = v.items;
    int sink = *(p - 1);    /* OOB expression under test: negative offset */

    v.drop();
    return sink;
}

int main(void) { return 0; }
