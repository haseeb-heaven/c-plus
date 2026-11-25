/* Case F: pointer arithmetic beyond a known array.
 *
 * `a + 5` is more than one-past-the-end of a 4-element array.  The extent is
 * known at compile time, so every configuration must reject the source with
 * a CPLUS-BOUNDS pointer-arithmetic diagnostic.
 */
#include <stdio.h>

int main(void) {
  int a[4] = {1, 2, 3, 4};
  int *p = a + 5;
  printf("%p\n", (void *)p);
  return 0;
}
