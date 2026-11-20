/* Bug 4: leak on early return.  A C+ resource is cleaned up on every exit. */
#include "cplus/cplus.h"

resource struct Buffer {
    int id;
};

void Buffer_drop(Buffer *self) {
    if (self->id != 0) {
        print("drop", self->id);
        self->id = 0;
    }
}

static void process(int trigger_error) {
    Buffer b = {7};
    if (trigger_error)
        return;
    (void)b;
}

int main(void) {
    process(1);
    print("done");
    return 0;
}
