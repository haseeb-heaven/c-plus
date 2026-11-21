#include "cplus/cplus.h"
resource struct H { int id; };
void H_drop(H *h) { if (h->id) h->id = 0; }
int main(void) { H a = {7}; H b = cplus_move(a); return b.id == 7 ? 0 : 1; }
