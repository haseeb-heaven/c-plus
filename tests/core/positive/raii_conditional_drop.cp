/* Wave-3 RAII regression: a conditional explicit drop must not remove cleanup
 * on the false path. Both paths together must produce exactly one drop each. */
#include "cplus/cplus.h"
static int drops = 0;
resource struct R { int id; };
void R_drop(R *r) { if (r->id) { drops++; r->id = 0; } }
static int run(int c) { R a = {1}; if (c) R_drop(&a); return 0; }
int main(void) { run(0); run(1); return drops == 2 ? 0 : 1; }
