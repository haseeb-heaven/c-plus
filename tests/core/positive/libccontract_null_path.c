#include <stdlib.h>
int main(void) {
  int *p = malloc(sizeof(int));
  if (p == NULL) return 1;  // allocation failure: nothing to release
  free(p);
  return 0;
}
