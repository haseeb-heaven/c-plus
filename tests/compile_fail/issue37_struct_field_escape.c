/* Issue #37: a local struct wrapping the address of an automatic local,
   returned through a helper that reads the wrapped field. The pointer
   escapes the frame and must be rejected with CPLUS-LIFETIME. */
#include "cplus/cplus.h"

struct Holder { int *p; };

int *unwrap(struct Holder *h) { return h->p; }

int *bad(void) {
    int x = 42;
    struct Holder h = { &x };
    return unwrap(&h);
}

int main(void) {
    int *q = bad();
    return *q;
}
