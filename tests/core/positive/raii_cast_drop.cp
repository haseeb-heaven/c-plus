/* Wave-3 RAII regression: an explicit drop through a cast must still suppress
 * the automatic drop (no double free). */
#include "cplus/cplus.h"
static int drops = 0;
resource struct R { int id; };
void R_drop(R *r) { if (r->id) { drops++; r->id = 0; } }
static int run(void) { R a = {1}; R_drop((R *)&a); return 0; }
int main(void) { run(); return drops == 1 ? 0 : 1; }
