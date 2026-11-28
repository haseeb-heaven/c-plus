/* Issue #68: a moved-from owner is invalidated at the caller.  Transferring
 * the same binding twice must be rejected, never handed to two callees. */
#include "cplus/cplus.h"

resource struct Box {
    int id;
};

void Box_drop(Box *b) { (void)b; }

int main(void) {
    Box r = {1};
    Box s = cplus_move(r);
    Box t = cplus_move(r);
    (void)s;
    (void)t;
    return 0;
}
