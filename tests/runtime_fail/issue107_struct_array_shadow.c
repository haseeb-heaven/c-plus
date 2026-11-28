/* Agent 1 issue #107: a struct array member named `buf` is not a binding of
 * a standalone pointer named `buf`; the pointer still needs a NULL check. */
#include "cplus/cplus.h"

struct Holder {
  int buf[4];
};

static void deref(int *buf) {
  *buf = 2;
}

int main(void) {
  struct Holder h;
  (void)h;
  deref(NULL);
  return 0;
}
