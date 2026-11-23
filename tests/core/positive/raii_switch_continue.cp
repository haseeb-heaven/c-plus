/* Wave-3 RAII regression: continue out of a switch nested in a loop must run
 * the enclosing loop's owner cleanup. */
#include "cplus/cplus.h"
static int drops = 0;
resource struct R { int id; };
void R_drop(R *r) { if (r->id) { drops++; r->id = 0; } }
int main(void) {
  for (int i = 0; i < 3; i++) {
    R r = {1};
    switch (i) { case 1: continue; default: break; }
  }
  return drops == 3 ? 0 : 1;
}
