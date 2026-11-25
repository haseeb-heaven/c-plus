/* P0 pointer-bounds reproducer: CONSTANT OOB on a stdlib-owned buffer via
 * pointer arithmetic.
 *
 * v.items is a stdlib_alloc-backed buffer owned by stdlib/vector.hp.  After
 * two push_back calls the logical extent is 2 (vector.used == 2).  The
 * expression *(p + 9) is 7 ints past the last live element.  A fixed-size
 * local array literal (a[9] over int[3]) IS rejected with a tagged
 * [CPLUS-BOUNDS] diagnostic; the same constant offset through a pointer into
 * a stdlib class buffer is not.
 *
 * The probe lives in a never-called function so the program itself contains
 * no executed UB; `c+ check --strict` still analyses the expression.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

static int pb_const_ptr_arith_oob(void) {
    vector v = vector();
    v.push_back(10);
    v.push_back(20);

    int *p = v.items;       /* pointer into stdlib-owned buffer, used == 2 */
    int sink = *(p + 9);    /* OOB expression under test: constant offset 9 */

    v.drop();
    return sink;
}

int main(void) { return 0; }
