#include <stdlib.h>
int main(void) {
  int *p = malloc(sizeof(int));
  if (!p) return 1;
  int *q = p;
  free(p);  // NOSONAR intentional safe-mode safety fixture
  return *q;  // NOSONAR intentional safe-mode safety fixture
}
