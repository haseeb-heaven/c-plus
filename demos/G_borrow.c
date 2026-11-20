/* Demo G: a valid borrow of a scoped resource.
 *
 * `borrow` points into `slot`; it is read while the owner is alive and is
 * never returned.  C+ rejects a borrow that escapes its function or is used
 * after the owner is moved, but accepts this pattern. */
#include <stdio.h>
#include "cplus/cplus.h"

resource struct Slot {
    int value;
};

void Slot_drop(Slot *self) {
    if (self->value != -1) {
        printf("drop slot %d\n", self->value);
        self->value = -1;
    }
}

static void read_slot(const int *p) {
    printf("borrowed value: %d\n", *p);
}

int main(void) {
    Slot slot = {42};
    int *borrow = &slot.value;
    read_slot(borrow);
    printf("owner still alive\n");
    return 0;
}
