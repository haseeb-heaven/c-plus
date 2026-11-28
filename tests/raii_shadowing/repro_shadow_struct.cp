/* Nested resource inside a struct field with a shadowed owner name. */
#include "cplus/cplus.h"
resource struct Inner { int id; };
void Inner_drop(Inner *s) { if (s->id) { print("DROP_INNER", s->id); s->id = 0; } }
resource struct Outer { Inner child; int id; };
void Outer_drop(Outer *s) {
    if (s->id) { print("DROP_OUTER", s->id); s->id = 0; }
}
int main(void) {
    Outer x = {{1}, 10};
    {
        Inner x = {2};
        print("FIELD_INNER", x.id);
    }
    print("FIELD_OUTER", x.id, x.child.id);
    return 0;
}
