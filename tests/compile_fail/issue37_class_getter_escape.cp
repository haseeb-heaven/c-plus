// Issue #37 (class/getter shape): a constructor stores `&local` into a field
// and a getter returns that field. Returning the getter's result from the
// constructing frame escapes the local and must be rejected.
#include "cplus/cplus.h"

class Box {
    int *p;
    Box(int *q) { this->p = q; }
    int *get(void) { return this->p; }
};

int *bad(void) {
    int x = 42;
    Box b = Box(&x);
    return b.get();
}

int main(void) {
    int *q = bad();
    return *q;
}
