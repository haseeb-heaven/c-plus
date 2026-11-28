/* P0 sound alias analysis — case 3: interior pointer captured before a
 * mutating operation, then used afterwards.
 * EXPECT: REJECT (P0 contract; currently ACCEPT — compiler capability gap).
 *
 * REQUESTED: REJECT with a tagged [CPLUS-BORROW] diagnostic naming 'p' as a
 * stale interior pointer of 's' after append() may reallocate.
 *
 * string::c_str() returns a pointer into the owned buffer; a sufficiently
 * large append reallocates it. The use of p after that mutation dangles.
 * Sound borrow analysis must connect the returned pointer to its source.
 */
#include "cplus/cplus.h"
#include "stdlib/string.hp"

int main(void) {
    string s = string("abc");
    const char *p = s.c_str(); /* interior pointer captured before mutation */

    s.append("defghijklmnopqrstuvwxyz"); /* can reallocate the owned buffer */
    char c = p[0];                       /* use of the stale interior pointer */

    s.drop();
    return c == 'a' ? 0 : 1;
}
