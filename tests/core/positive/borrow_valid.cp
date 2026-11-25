#include "cplus/cplus.h"
resource struct H { int id; };
void H_drop(H *h) { if (h->id) h->id = 0; }
static int read(H *h) { return h->id; }
int main(void) { H a = {7}; H *b = &a; return read(b) == 7 ? 0 : 1; }
