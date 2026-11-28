/* Issue #68 reproducer: by-value resource parameters would hand the same
 * handle to the callee twice.  C+ rejects the by-value signature, so no
 * name-based transfer bookkeeping (and no double free) is possible. */
#include "cplus/cplus.h"

resource struct Box {
    int id;
};

void Box_drop(Box *b) { (void)b; }

static void consume(Box b) { (void)b; }

static void helper(void) {
    Box r = {1};
    consume(r);
    consume(r);
}

int main(void) {
    Box r = {2};
    helper();
    return 0;
}
