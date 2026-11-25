/* NEGATIVE lifetime: the same owner is released twice. */
#include "cplus/cplus.h"
int main(void) { int *p = cplus_alloc(sizeof(int)); if (!p) return 1; cplus_release(p); cplus_release(p); return 0; }
