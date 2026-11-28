/* Reproducer for issue #28: shadowed owned locals with distinct resources.
 * Two R bindings share the name `x` in nested scopes; each must be destroyed
 * exactly once, inner before outer. */
#include "cplus/cplus.h"

resource struct R { int id; };
void R_drop(R *self) {
    if (self->id) { print("DROP", self->id); self->id = 0; }
}

int main(void) {
    R x = {1};
    print("OUTER", x.id);
    {
        R x = {2};
        print("INNER", x.id);
    }
    print("AFTER_INNER", x.id);
    return 0;
}
