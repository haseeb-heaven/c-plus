/* Cake _View: a borrow is non-owning and must not be moved. */
#include "cplus/cplus.h"
int main(void) {
  int value = 7;
  int *_View view = &value;
  int *a = cplus_move(view);
  return *a;
}
