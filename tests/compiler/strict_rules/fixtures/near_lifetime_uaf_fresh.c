/* ADVERSARIAL near-miss (LIFETIME-UAF): release, then use a fresh owner. */
#include "cplus/cplus.h"
int main(void) { int *p = cplus_alloc(sizeof(int)); if (!p) return 1; *p = 42; cplus_release(p); int *q = cplus_alloc(sizeof(int)); if (!q) return 1; *q = 7; int v = *q; cplus_release(q); return v == 7 ? 0 : 1; }
