/* BUG 545: realloc return value ignored, then the old pointer is used. */
#include <stdlib.h>

void bug_ignored_realloc(void) {
  int *p = malloc(4 * sizeof(int));
  if (!p)
    return;
  realloc(p, 64 * sizeof(int));
  // BUG: if the block moved, p is dangling; if it failed, capacity did not
  // grow. The result must be captured and checked.
  p[0] = 1;
  free(p);
}

int main(void) {
  bug_ignored_realloc();
  return 0;
}
