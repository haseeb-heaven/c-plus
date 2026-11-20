#include "cplus/cplus.h"

resource struct OwnedHandle {
    int id;
};

void OwnedHandle_drop(OwnedHandle *self) {
    self->id = 0;
}

int main(void) {
    OwnedHandle first = {11};
    OwnedHandle second = first; /* Must reject: two owners for one resource. */
    print(second.id);
    return 0;
}
