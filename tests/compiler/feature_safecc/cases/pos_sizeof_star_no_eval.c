/* issue #180: sizeof does not evaluate its operand.  p is must-NULL on every
 * path to `sizeof *p`, but the unparenthesized operand is unevaluated, so
 * [CPLUS-SAFECC] must stay silent.  The if/else join keeps this independent of
 * the older lifetime diagnostic. */
#include <stddef.h>
int f(int c) {
  int *p;
  if (c) p = NULL;
  else p = (int *)NULL;
  return (int)sizeof *p;
}
int main(void) { return f(1); }
