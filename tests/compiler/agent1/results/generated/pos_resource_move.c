#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/resource_move.c"
#include <stdio.h>
#include "cplus/cplus.h"

 struct Handle {
    int id;
};
typedef struct Handle Handle;
#line 8 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/resource_move.c"


static int drops = 0;

void Handle_drop(Handle *self) {
    if (self->id != 0) {
        printf("drop %d\n", self->id);
        drops++;
        self->id = 0;
    }
}

Handle Handle_make(int id) {
    Handle h = {0};
    h.id = id;
    return h;
Handle_drop(&h); 
#line 26 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/resource_move.c"
}

int main(void) {
    Handle a = Handle_make(1);
    Handle b = cplus_move(a);
    printf("a=%d b=%d\n", a.id, b.id);
    { __typeof__(0) __cplus_ret = (0); Handle_drop(&b); Handle_drop(&a);  return __cplus_ret;; }
Handle_drop(&b); Handle_drop(&a); 
#line 33 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/resource_move.c"
}