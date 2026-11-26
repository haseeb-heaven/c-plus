/* Agent 1 issue #107: an array named `p` in an unrelated function must not
 * suppress the runtime NULL check for a pointer parameter named `p`. */
#include "cplus/cplus.h"

static void helper(void) {
  int p[4] = {0};
  (void)p;
}

static void deref(int *p) {
  *p = 1;
}

int main(void) {
  helper();
  deref(NULL);
  return 0;
}
