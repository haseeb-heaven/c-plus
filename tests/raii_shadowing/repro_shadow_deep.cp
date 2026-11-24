/* Three nested scopes, all naming their owned binding `x`. */
#include "cplus/cplus.h"
resource struct R { int id; };
void R_drop(R *self) { if (self->id) { print("DROP", self->id); self->id = 0; } }
int main(void) {
    R x = {1};
    {
        R x = {2};
        {
            R x = {3};
            print("L3", x.id);
        }
        print("L2", x.id);
    }
    print("L1", x.id);
    return 0;
}
