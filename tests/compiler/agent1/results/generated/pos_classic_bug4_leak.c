#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/classic_bug4_leak.c"
/* Bug 4: leak on early return.  A C+ resource is cleaned up on every exit. */
#include "cplus/cplus.h"

 struct Buffer {
    int id;
};
typedef struct Buffer Buffer;
#line 8 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/classic_bug4_leak.c"


void Buffer_drop(Buffer *self) {
    if (self->id != 0) {
        print("drop", self->id);
        self->id = 0;
    }
}

static void process(int trigger_error) {
    Buffer b = {7};
    if (trigger_error)
        { Buffer_drop(&b); return; }
    (void)b;
Buffer_drop(&b); 
#line 24 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/classic_bug4_leak.c"
}

int main(void) {
    process(1);
    print("done");
    return 0;
}
