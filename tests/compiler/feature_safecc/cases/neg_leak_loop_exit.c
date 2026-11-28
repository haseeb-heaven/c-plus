/* issue #180: allocation survives the loop back-edge and every exit. */
#include <stdlib.h>
int f(void) {
  int *p = malloc(8);
  for (int i = 0; i < 2; i++) {
    (void)i;
  }
  return 0;
}
int main(void) { return f(); }
