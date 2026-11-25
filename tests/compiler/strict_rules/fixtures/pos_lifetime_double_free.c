/* POSITIVE lifetime: one owner, one release. */
#include "cplus/cplus.h"
int main(void) { int *p = cplus_alloc(sizeof(int)); if (!p) return 1; *p = 7; int v = *p; cplus_release(p); return v == 7 ? 0 : 1; }
