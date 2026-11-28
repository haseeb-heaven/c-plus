/* Bug 761 (catalog scratch/bugs49.cp): a forward goto jumps into the scope
 * of a variable-length array, leaving the VLA without a defined size or
 * lifetime at the label. */
#include <stdio.h>

int main(void) {
  int n = 4;
  goto skip;
  int a[n];
skip:
  printf("%zu\n", sizeof a); /* BUG 761: a is in scope without a declaration */
  return 0;
}
