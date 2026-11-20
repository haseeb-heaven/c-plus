#include "cplus/cplus.h"

typedef struct Buf { int size; } Buf;

Buf *make(void) {
    managed Buf *b = cplus_new(Buf);
    b->size = 1;
    return b;
}

int main(void) { (void)make(); return 0; }
