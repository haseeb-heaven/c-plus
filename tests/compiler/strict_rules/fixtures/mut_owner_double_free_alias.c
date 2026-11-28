/* ADVERSARIAL mutation (OWNER-DOUBLE-FREE / LIFETIME-DOUBLE-FREE): release via alias. */
#include "cplus/cplus.h"
int main(void) { int *p = cplus_alloc(sizeof(int)); if (!p) return 1; int *q = p; cplus_release(p); cplus_release(q); return 0; }
