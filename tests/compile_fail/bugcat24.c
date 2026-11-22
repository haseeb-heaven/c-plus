/* BUG 505: realloc failure loses the original pointer. */
#include <stdlib.h>

void grow(int **slot, size_t n) {
  /* Buggy: overwriting the only pointer means a failed realloc returns NULL
     and both the original block and the new size are lost. */
  *slot = realloc(*slot, n);
}

int main(void) {
  int *p = malloc(4 * sizeof(int));
  if (p)
    grow(&p, 1024 * sizeof(int));
  free(p);
  return 0;
}
