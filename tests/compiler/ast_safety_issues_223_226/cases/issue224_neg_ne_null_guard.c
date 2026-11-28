/* issue #224: `p != NULL` must not be treated as proving p non-null. */
#include <stddef.h>

int f(int *p) {
  p = NULL;
  if (p != NULL) return 0;
  return *p;
}
