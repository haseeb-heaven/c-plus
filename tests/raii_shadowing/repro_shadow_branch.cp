/* Shadowed owned locals in if/else branches. */
#include "cplus/cplus.h"
resource struct R { int id; };
void R_drop(R *self) { if (self->id) { print("DROP", self->id); self->id = 0; } }
int main(int argc, char **argv) {
    R x = {1};
    if (argc > 0) {
        R x = {2};
        print("THEN", x.id);
    } else {
        R x = {3};
        print("ELSE", x.id);
    }
    print("AFTER", x.id);
    return 0;
}
