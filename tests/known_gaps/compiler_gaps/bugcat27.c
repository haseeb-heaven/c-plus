/* BUG 536: double free via a copied handle/alias. */
#include <stdlib.h>

void bug_double_free_alias(void) {
  int *p = malloc(sizeof(int));
  if (!p)
    return;
  int *alias = p;
  free(p);
  // BUG: alias still refers to the same released block.
  free(alias);
}

int main(void) {
  bug_double_free_alias();
  return 0;
}
