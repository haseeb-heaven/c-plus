#include <stdio.h>
#include "cplus/cplus.h"

resource struct Handle { int id; };
void Handle_drop(Handle *h) { if (h->id) h->id = 0; }

int main(void) {
    Handle a = {7};
    Handle *b = &a;
    Handle c = cplus_move(a);
    printf("%d\n", b->id);
    (void)c;
    return 0;
}
