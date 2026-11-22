/* BUG 515: zero-size allocation is dereferenced. */
#include <stdlib.h>

void bug_zero_size(void) {
  int *p = malloc(0);
  // BUG: malloc(0) has implementation-defined behavior; p may be NULL or a
  // zero-size block, so writing through it is undefined.
  p[0] = 1;
  free(p);
}

int main(void) {
  bug_zero_size();
  return 0;
}
