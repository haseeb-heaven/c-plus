/* Case A: array-decay bounds.
 *
 * `a` has a recorded extent of 4 ints.  Passing it to `first(int *p)` must
 * carry that extent across the call, so p[4] (one past the last element) is a
 * proven out-of-bounds access and must be rejected at compile time.
 */
#include <stdio.h>

int first(int *p) { return p[4]; }

int main(void) {
  int a[4] = {1, 2, 3, 4};
  printf("%d\n", first(a));
  return 0;
}
