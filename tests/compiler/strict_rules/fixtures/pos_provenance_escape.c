/* POSITIVE pointer escape: copy the pointee out; do not return the managed pointer. */
#include "cplus/cplus.h"
typedef struct Buf { int size; } Buf;
static Buf make(void) { managed Buf *b = cplus_new(Buf); b->size = 1; Buf copy = *b; return copy; }
int main(void) { return make().size == 1 ? 0 : 1; }
