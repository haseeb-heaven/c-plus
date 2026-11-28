#include "cplus/cplus.h"
resource struct Handle { int id; };
void Handle_drop(Handle *h) { if (h->id) h->id = 0; }
int main(void) {
  Handle a = {9};
  Handle *borrow = &a;   /* valid before the move */
  Handle b = cplus_move(a);
  Handle *fresh = &b;    /* re-borrow the new owner after the move */
  return fresh->id == 9 ? 0 : 1;
}
