#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/libccontract_null_path.c"
#include <stdlib.h>
int main(void) {
  int *p = malloc(sizeof(int));
  if (p == NULL) return 1;  // allocation failure: nothing to release
  free(p);
  return 0;
}