/* NEGATIVE pointer escape: managed pointer returned past its owning scope. */
#include "cplus/cplus.h"
typedef struct Buf { int size; } Buf;
static Buf *make(void) { managed Buf *b = cplus_new(Buf); b->size = 1; return b; }
int main(void) { return make() != 0 ? 0 : 1; }
