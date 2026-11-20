#include <stdio.h>
#include "cplus/cplus.h"

resource struct Handle {
    int id;
};

static int drops = 0;

void Handle_drop(Handle *self) {
    if (self->id != 0) {
        printf("drop %d\n", self->id);
        drops++;
        self->id = 0;
    }
}

Handle Handle_make(int id) {
    Handle h;
    h.id = id;
    return h;
}

int main(void) {
    Handle a = Handle_make(1);
    Handle b = cplus_move(a);
    printf("a=%d b=%d\n", a.id, b.id);
    return 0;
}
