/* NOT DETECTED: after a released block's address is reused, a stale alias
 * can be misattributed to the new allocation. Documented gap for issue #1. */
#include "cplus/cplus.h"
int main(void) {
  int *a = cplus_alloc(sizeof(int));
  cplus_release(a);
  int *b = cplus_alloc(sizeof(int));
  cplus_release(a); /* may silently release b's block if addresses match */
  cplus_release(b);
  return 0;
}
