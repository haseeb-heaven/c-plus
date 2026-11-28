#include "cplus/cplus.h"
static int drops = 0;
resource struct R { int id; };
void R_drop(R *r) { if (r->id) { drops++; r->id = 0; } }
int main(void) {
  for (int i = 0; i < 4; i++) {
    R r = {i + 1};
    if (i == 1) continue;
    if (i == 3) break;
  }
  return drops == 4 ? 0 : 1;
}
