/* issue #180: p is NULL on the only path to *p. */
#include <stddef.h>
int main(void) {
  int *p = NULL;
  return *p;
}
