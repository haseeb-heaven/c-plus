/* ADVERSARIAL near-miss (OWNER-DOUBLE-FREE): two syntactic releases, one per path. */
#include "cplus/cplus.h"
int main(void) { int *p = cplus_alloc(sizeof(int)); if (!p) return 1; if (p) cplus_release(p); else cplus_release(p); return 0; }
