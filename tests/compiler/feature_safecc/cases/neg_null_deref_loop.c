/* issue #180: the loop back-edge re-establishes NULL on every iteration. */
#include <stddef.h>
int f(int c) {
  int *p = NULL;
  for (int i = 0; i < 3; i++) {
    p = NULL;
  }
  (void)c;
  return *p;
}
int main(void) { return f(0); }
