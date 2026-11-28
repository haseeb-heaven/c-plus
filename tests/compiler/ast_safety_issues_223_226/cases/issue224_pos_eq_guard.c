/* issue #224: a real `== 0` early return still proves x non-zero. */
#include <stddef.h>

int f(int x, int *p) {
  if (x == 0) return 0;
  if (p == NULL) return 0;
  return (1 / x) + *p;
}
