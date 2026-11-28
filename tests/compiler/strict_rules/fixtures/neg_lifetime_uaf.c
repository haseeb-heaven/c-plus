/* NEGATIVE lifetime: explicit liveness check on a released pointer. */
#include "cplus/cplus.h"
int main(void) { int *p = cplus_alloc(sizeof(int)); if (!p) return 1; *p = 42; cplus_release(p); cplus_check_live(p); return 0; }
