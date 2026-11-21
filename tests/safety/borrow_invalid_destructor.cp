#include "cplus/cplus.h"

resource struct R { int x; };
void R_drop(R *r) { if (r->x) { r->x = 0; } }

int main(void) {
    R r = {7};
    int *b = &r.x;
    R_drop(&r);
    return *b;  // must be rejected: borrow used after owner destruction
}
