/* Case C: runtime division/modulo by zero.
 *
 * Both operands come from stdin.  op == 0 exercises `x / d`; op == 1
 * exercises `x % d`.  With d == 0 both operations are undefined; a native
 * guard (110) or UBSan must trap.  On arm64 the raw hardware division would
 * otherwise silently return 0 and the dividend, so a rc-0 run is a failure.
 */
#include <stdio.h>

int main(void) {
  int d = 1;
  int op = 0;
  if (scanf("%d %d", &d, &op) != 2)
    return 2;
  int x = 100;
  if (op == 0)
    printf("%d\n", x / d);
  else
    printf("%d\n", x % d);
  return 0;
}
