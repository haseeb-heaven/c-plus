#include "cplus/cplus.h"
static int drops = 0;
resource struct R { int id; };
void R_drop(R *r) { if (r->id) { drops++; r->id = 0; } }
static int recurse(int n) {
  R r = {n};
  if (n <= 1) return 1;
  return recurse(n - 1) + n;
}
int main(void) { return recurse(4) == 10 && drops == 4 ? 0 : 1; }
