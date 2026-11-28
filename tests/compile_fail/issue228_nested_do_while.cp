/* Issue #228 compile-fail companion: the nested `do { while (...) break; }
 * while (...)` shape must still report its ownership diagnostics verbatim. */
#include "cplus/cplus.h"

resource struct Box { int id; };

void Box_drop(Box *b) { (void)b; }

int main(void) {
  int n = 1;
  do {
    Box outerR = {1};
    while (n) {
      Box innerR = {2};
      Box movedR = cplus_move(innerR);
      Box twiceR = cplus_move(innerR);
      (void)movedR;
      (void)twiceR;
      (void)outerR;
      break;
    }
  } while (0);
  return 0;
}
