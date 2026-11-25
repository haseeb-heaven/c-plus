/* Safety matrix: verifies scope cleanup actually invokes the resource drop. */
#include "cplus/cplus.h"

static int drops = 0;
resource struct R { int id; };
void R_drop(R *r) { if (r->id) { drops++; r->id = 0; } }

static int use(void) {
  R r = {7};
  return r.id;
}

int main(void) {
  int v = use();
  return (v == 7 && drops == 1) ? 0 : 1;
}
