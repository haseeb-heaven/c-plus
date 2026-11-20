#include <stdio.h>

resource struct R { int id; };

void R_drop(R *self) { printf("drop %d\n", self->id); }

R R_make(int id) { R r; r.id = id; return r; }

int main(void) {
    R a = R_make(1);
    {
        R b = R_make(2);
        {
            R c = R_make(3);
            (void)c;
        }
        (void)b;
    }
    R d = R_make(4);
    (void)d;
    (void)a;
    return 0;
}
