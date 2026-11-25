/* POSITIVE realloc: managed reallocation with an explicit success check. */
#include "cplus/cplus.h"
int main(void) { int *p = cplus_realloc(0, sizeof(int)); if (!p) return 1; *p = 9; int v = *p; cplus_release(p); return v == 9 ? 0 : 1; }
