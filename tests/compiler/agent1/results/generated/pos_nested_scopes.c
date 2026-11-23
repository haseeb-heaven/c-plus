#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/nested_scopes.c"
#include <stdio.h>

 struct R { int id; };
typedef struct R R;
#line 5 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/nested_scopes.c"


void R_drop(R *self) { printf("drop %d\n", self->id); }

R R_make(int id) { R r = {0}; r.id = id; return r; R_drop(&r); 
#line 11 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/nested_scopes.c"
}

int main(void) {
    R a = R_make(1);
    {
        R b = R_make(2);
        {
            R c = R_make(3);
            (void)c;
        R_drop(&c); 
#line 20 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/nested_scopes.c"
}
        (void)b;
    R_drop(&b); 
#line 22 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/nested_scopes.c"
}
    R d = R_make(4);
    (void)d;
    (void)a;
    { __typeof__(0) __cplus_ret = (0); R_drop(&d); R_drop(&a);  return __cplus_ret;; }
R_drop(&d); R_drop(&a); 
#line 27 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/nested_scopes.c"
}
