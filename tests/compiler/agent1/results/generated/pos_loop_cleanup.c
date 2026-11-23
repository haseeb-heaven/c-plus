#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/loop_cleanup.c"
#include <stdio.h>

 struct R { int id; };
typedef struct R R;
#line 5 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/loop_cleanup.c"


void R_drop(R *self) { printf("drop %d\n", self->id); }

R R_make(int id) { R r = {0}; r.id = id; return r; R_drop(&r); 
#line 11 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/loop_cleanup.c"
}

int main(void) {
    int i;
    for (i = 0; i < 4; i++) {
        R r = R_make(i);
        if (i == 1)
            { R_drop(&r); continue; }
        if (i == 3)
            { R_drop(&r); break; }
    R_drop(&r); 
#line 21 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/loop_cleanup.c"
}
    printf("loop done\n");
    return 0;
}