#include "cplus/cplus.h"
static int drops = 0;
resource struct R { int id; };
void R_drop(R *r) { if (r->id) { drops++; r->id = 0; } }
int main(void) {
  { R a = {1}; R_drop(&a); }
  { R b = {2}; }
  return drops == 2 ? 0 : 1;
}
