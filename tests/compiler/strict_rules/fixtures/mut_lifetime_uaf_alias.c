/* ADVERSARIAL mutation (LIFETIME-UAF): liveness check on an alias after release. */
#include "cplus/cplus.h"
int main(void) { int *p = cplus_alloc(sizeof(int)); if (!p) return 1; int *q = p; cplus_release(p); cplus_check_live(q); return 0; }
