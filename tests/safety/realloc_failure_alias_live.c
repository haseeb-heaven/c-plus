#include <stdlib.h>
int main(void) {
  int *p = malloc(sizeof(int));
  if (!p) return 2;
  int *old = p;
  void *t = realloc(p, 2 * sizeof(int));
  if (!t) { *old = 7; free(old); return 0; }  // original still valid on failure
  p = t;
  free(p);
  return 0;
}
