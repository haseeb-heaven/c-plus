/* issue #225: the RHS of `p || *p` runs only when p is NULL. */
#include <stddef.h>

int f(void) {
  int *p = NULL;
  return (p || *p);
}
