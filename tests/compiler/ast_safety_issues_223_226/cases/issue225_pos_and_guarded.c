/* issue #225: the RHS of `p && *p` is a valid guarded dereference. */
#include <stddef.h>

int f(int *p) {
  if (p && *p > 0)
    return *p;
  return 0;
}
