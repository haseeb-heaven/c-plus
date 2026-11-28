/* issue #224: `if (!a) return;` proves only a non-null, not b. */
#include <stddef.h>

int f(void) {
  int *a = NULL, *b = NULL;
  if (!a) return 0;
  return *b;
}
