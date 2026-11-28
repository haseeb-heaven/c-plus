/* P0 pointer-bounds reproducer: canonical class-owned constant OOB.
 *
 * This mirrors docs/libc/AGENT1_COMPILER_GAPS.md capability 5: vector::items
 * is a stdlib-owned buffer, vector.used is 2, and at_checked(9, &out) passes a
 * constant index that is statically out of range.  The index arrives through
 * an ordinary method parameter, which the current compiler does not relate
 * to the receiver's extent, so the file compiles clean under --strict.
 *
 * Unlike the raw pointer-arithmetic probes, this API's runtime contract is
 * to return -1 (and note bounds telemetry) rather than dereference; the
 * compile-time gap is the missing tagged diagnostic.
 *
 * The probe lives in a never-called function so the program contains no
 * executed UB; the check still sees the expression.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

static int pb_class_at_checked_const_oob(void) {
    vector v = vector();
    v.push_back(10);
    v.push_back(20);

    int out = -1;
    int rc = v.at_checked(9, &out);  /* constant index 9, used == 2 */

    v.drop();
    return rc;
}

int main(void) { return 0; }
