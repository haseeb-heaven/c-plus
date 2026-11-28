/* POSITIVE lifetime: use before release, then release exactly once. */
#include "cplus/cplus.h"
int main(void) { int *p = cplus_alloc(sizeof(int)); if (!p) return 1; *p = 42; int v = *p; cplus_release(p); return v == 42 ? 0 : 1; }
