/* Issue #68: transfer state is per binding, never a translation-unit-wide
 * name set.  Two unrelated functions may both have a local named `r`, and a
 * conditional move must not suppress the drop on the untaken branch. */
#include <stdio.h>
#include "cplus/cplus.h"

resource struct Box {
    int id;
};

void Box_drop(Box *b) {
    printf("drop %d\n", b->id);
    b->id = 0;
}

static void helper(void) {
    Box r = {1};
    Box s = cplus_move(r);
    (void)s;
}

static void maybe(int take) {
    Box r = {7};
    if (take) {
        Box s = cplus_move(r);
        (void)s;
    }
}

int main(void) {
    Box r = {2};
    printf("main r=%d\n", r.id);
    helper();
    maybe(0);
    maybe(1);
    return 0;
}
