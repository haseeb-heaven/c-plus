#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/realloc_chain.c"
#include "cplus/cplus.h"
int main(void) {
  int *p = cplus_realloc(NULL, sizeof(int));
  for (int i = 0; i < 16; i++) {
    int *q = cplus_realloc(p, sizeof(int) * (size_t)(i + 2));
    if (!q) return 1;
    p = q;
    p[i + 1] = i;
  }
  int ok = p[16] == 15;
  cplus_release(p);
  return ok ? 0 : 1;
}