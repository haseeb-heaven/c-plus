/* Shadowed owned local in a loop body, with break/continue. */
#include "cplus/cplus.h"
resource struct R { int id; };
void R_drop(R *self) { if (self->id) { print("DROP", self->id); self->id = 0; } }
int main(void) {
    R x = {1};
    for (int i = 1; i <= 3; ++i) {
        R x = {10 + i};
        if (i == 1) continue;
        if (i == 3) break;
        print("LOOP", x.id);
    }
    print("AFTER", x.id);
    return 0;
}
