#include "cplus/cplus.h"
static int seq = 0;
resource struct R { int id; };
void R_drop(R *r) { if (r->id) { seq = seq * 10 + r->id; r->id = 0; } }
int main(void) {
  { R a = {1}; { R b = {2}; { R c = {3}; } R d = {4}; } R e = {5}; }
  return seq == 34251 ? 0 : 1;
}
