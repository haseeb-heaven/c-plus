/* Cross-TU: owned type declared locally as a resource, destructor and factory
 * defined in another translation unit with the same C layout. */
#include "cplus/cplus.h"
resource struct R { int id; };
void R_drop(struct R *self);
struct R make_r(int id);
int main(void) {
    struct R x = make_r(1);
    { struct R x = make_r(2); print("INNER", x.id); }
    print("OUTER", x.id);
    return 0;
}
