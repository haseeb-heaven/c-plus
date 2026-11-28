/* Partially initialised / failed resource in a shadowed scope. */
#include "cplus/cplus.h"
resource struct R { int id; };
void R_drop(R *self) { if (self->id) { print("DROP", self->id); self->id = 0; } }
R make(int id, int fail) {
    R r = {0};
    if (fail) return r;      /* no resource acquired: destructor must be inert */
    r.id = id;
    return r;
}
int main(void) {
    R x = {1};
    {
        R x = make(2, 0);
        print("OK", x.id);
    }
    {
        R x = make(3, 1);
        print("FAIL", x.id);
    }
    print("OUTER", x.id);
    return 0;
}
