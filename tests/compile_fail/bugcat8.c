/* Bug 328: sizeof on a VLA evaluates its size operand at run time, so a side
 * effect inside the sizeof escapes into observable state. */
#include <stdio.h>

void bad_sizeof(int n) {
  int a[n++];
  size_t bytes = sizeof a;
  printf("%zu %d\n", bytes, n);
}

int main(void) { return 0; }
