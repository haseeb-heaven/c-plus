/* Cake _Owner: reassignment at the top of each loop iteration feeds the move. */
#include "cplus/cplus.h"
int main(int argc, char **argv) {
  int a = 1, b = 2;
  int *_Owner p = &a;
  for (int i = 0; i < argc; ++i) {
    p = (i & 1) ? &a : &b;
    int *x = cplus_move(p);
    (void)x;
  }
  return 0;
}
