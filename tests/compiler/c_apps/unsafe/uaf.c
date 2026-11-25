/* Unsafe: use after free. STRICT must reject. */
#include <stdlib.h>
int main(void) {
  int *p = (int *)malloc(sizeof(int));
  if (!p) return 1;
  free(p);
  return *p;
}
