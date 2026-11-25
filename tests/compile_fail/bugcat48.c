/* Bug 752 (catalog scratch/bugs48.cp): sizeof on a VLA evaluates the size
 * expression at run time, so the increment in the subscript is a real side
 * effect even though sizeof looks compile-time. */
#include <stdio.h>

int main(void) {
  int n = 4;
  int a[n];
  printf("%zu\n", sizeof(a[n++])); /* BUG 752: n++ runs inside sizeof */
  return 0;
}
