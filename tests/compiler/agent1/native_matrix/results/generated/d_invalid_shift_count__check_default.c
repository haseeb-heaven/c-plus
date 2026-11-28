#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/agent1/native_matrix/cases/d_invalid_shift_count.c"
/* Case D: invalid shift count (negative and >= width).
 *
 * The count is read from stdin.  Two runs are required per build config:
 *   -1  -> negative shift count
 *   32  -> count >= the 32-bit width of the left operand
 * A native guard (110) or UBSan must trap both.
 */
#include <stdio.h>

int main(void) {
  int n = 0;
  if (scanf("%d", &n) != 1)
    return 2;
  unsigned x = 1u;
  printf("%u\n", x << n);
  return 0;
}