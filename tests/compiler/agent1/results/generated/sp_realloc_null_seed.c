#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/realloc_null_seed.c"
#include <stdlib.h>
int main(void) {
  int *p = realloc(0, sizeof(int));  // realloc(NULL,n) == malloc(n)
  if (!p) return 2;
  *p = 9;
  int v = *p;
  free(p);
  return v == 9 ? 0 : 1;
}