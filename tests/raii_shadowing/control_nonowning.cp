/* Control case: shadowed NON-owning locals (plain ints). */
#include "cplus/cplus.h"
resource struct R { int id; };
void R_drop(R *self) { if (self->id) { print("DROP", self->id); self->id = 0; } }
int main(void) {
    R keep = {1};
    int x = 10;
    { int x = 20; print("INNER", x); }
    print("OUTER", x, keep.id);
    return 0;
}
