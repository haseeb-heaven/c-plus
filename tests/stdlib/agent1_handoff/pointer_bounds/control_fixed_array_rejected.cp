/* CONTROL case (tagged REJECT): fixed-array literal OOB.
 *
 * This is the one case the current compiler ALREADY catches.  The literal
 * subscript a[9] on a local int[3] is rejected with
 *   [CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS] array index 9 exceeds length 3
 * It is included to show the tagging the stdlib class cases lack; the
 * pointer-arithmetic cases in this directory compile clean despite being
 * equally (or more) obviously out of range.
 *
 * The probe lives in a never-called function; the check still analyses it.
 */
#include "cplus/cplus.h"

static int pb_control_fixed_array(void) {
    int a[3] = {1, 2, 3};
    return a[9];   /* tagged [CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS] */
}

int main(void) { return 0; }
