#include <stdlib.h>
int main(void) {
  int *p = malloc(16 * sizeof(int));
  if (!p) return 2;
  int offset = 2;
  free(p + offset);   // provably interior pointer
  return 0;
}
