#include "cplus/cplus.h"
resource struct H { int id; };
void H_drop(H *h) { if (h->id) h->id = 0; }
int main(void) { H a = {1}; H b = cplus_move(a); return a.id + b.id == 1 ? 0 : 1; }
