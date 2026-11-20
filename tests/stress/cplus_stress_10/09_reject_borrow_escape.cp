#include "cplus/cplus.h"

resource struct Owner {
    int id;
};

void Owner_drop(Owner *self) {
    self->id = 0;
}

Owner *leak_borrow(void) {
    Owner local = {7};
    Owner *borrow = &local;
    return borrow; /* Must reject: owner dies at function return. */
}

int main(void) {
    Owner *ptr = leak_borrow();
    print(ptr->id);
    return 0;
}
