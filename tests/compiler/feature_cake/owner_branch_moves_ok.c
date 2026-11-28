/* Cake _Owner: one move in each arm of an if/else is not a double move. */
#include "cplus/cplus.h"
int main(int argc, char **argv) {
  int value = 7;
  int *_Owner p = &value;
  if (argc > 1) {
    int *a = cplus_move(p);
    (void)a;
  } else {
    int *b = cplus_move(p);
    (void)b;
  }
  return 0;
}
