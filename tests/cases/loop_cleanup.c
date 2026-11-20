#include <stdio.h>

resource struct R { int id; };

void R_drop(R *self) { printf("drop %d\n", self->id); }

R R_make(int id) { R r; r.id = id; return r; }

int main(void) {
    int i;
    for (i = 0; i < 4; i++) {
        R r = R_make(i);
        if (i == 1)
            continue;
        if (i == 3)
            break;
    }
    printf("loop done\n");
    return 0;
}
