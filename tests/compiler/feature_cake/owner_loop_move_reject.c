/* Cake _Owner: a move in a loop body is repeated on the next iteration. */
#include "cplus/cplus.h"
int main(int argc, char **argv) {
  int value = 7;
  int *_Owner p = &value;
  for (int i = 0; i < argc; ++i) {
    int *a = cplus_move(p);
    (void)a;
  }
  return 0;
}
