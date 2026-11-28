// Issue #36: a method returning `this` (lowered to a pointer parameter) hands
// back the address of a stack instance, which escapes through the caller's
// return. STRICT must reject it with CPLUS-LIFETIME.
#include "cplus/cplus.h"

class Box {
    int value;
    Box *self(void) { return this; }
};

Box *make_box(void) {
    Box b;
    return b.self();
}

int main(void) {
    Box *p = make_box();
    return p == 0;
}
