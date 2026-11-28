#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/agent1/native_matrix/cases/b_runtime_signed_overflow.c"
/* Case B: runtime signed overflow.
 *
 * The operand is read from stdin, so no static proof is possible.  Feeding
 * 2147483647 makes `x + 1` overflow a 32-bit signed int at runtime.  A native
 * guard (CPLUS_EXIT_ARITHMETIC = 110) or UBSan must trap; wrapping silently
 * is a failure.
 */
#include <stdio.h>

static int add_one(int x) { return x + 1; }

int main(void) {
  int x = 0;
  if (scanf("%d", &x) != 1)
    return 2;
  printf("%d\n", add_one(x));
  return 0;
}