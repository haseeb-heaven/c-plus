// OP: raw pointer arithmetic on a stdlib object.
// A string_view over a managed `string` exposes data(); this test walks far
// past the view's extent with raw `p + i` arithmetic instead of the checked
// accessors. The extent is known to the object but the raw arithmetic carries
// no proof that `p + 100` stays in bounds.
#include "cplus/cplus.h"
#include "stdlib/string.hp"

int main(void) {
    string s = string("abc");
    string_view v = s.to_view();
    const char *p = v.data();   /* pointer into a stdlib-managed object */
    char c = *(p + 100);        /* raw arithmetic, no bounds proof */
    (void)c;
    return 0;
}
