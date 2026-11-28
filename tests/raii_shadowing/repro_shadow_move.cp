/* Moved-from shadowed owner: b takes ownership from inner x; inner x must be
 * inert; outer x must still be destroyed exactly once. */
#include "cplus/cplus.h"
resource struct R { int id; };
void R_drop(R *self) { if (self->id) { print("DROP", self->id); self->id = 0; } }
int main(void) {
    R x = {1};
    {
        R x = {2};
        R moved = cplus_move(x);
        print("MOVED", x.id, moved.id);
    }
    print("OUTER", x.id);
    return 0;
}
