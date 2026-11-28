/* Early return from a scope containing a shadowed owned local. */
#include "cplus/cplus.h"
resource struct R { int id; };
void R_drop(R *self) { if (self->id) { print("DROP", self->id); self->id = 0; } }
int helper(int which) {
    R x = {1};
    if (which == 2) {
        R x = {2};
        print("INNER_RET", x.id);
        return 5;
    }
    print("OUTER", x.id);
    return 7;
}
int main(void) { print("RET", helper(2)); return 0; }
