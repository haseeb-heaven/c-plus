#include <stddef.h>
extern void *cplus_register_allocation(void *, size_t, const char *, int);
extern void cplus_free_registered(void *, const char *, int);
extern void *cplus_realloc_registered(void *, void *, size_t, const char *, int);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/realloc_null_seed.c"
#include <stdlib.h>
int main(void) {
  int *p = cplus_realloc_registered((0), (realloc(0, sizeof(int))), 4, __FILE__, __LINE__);  // realloc(NULL,n) == malloc(n)
  if (!p) return 2;
  *p = 9;
  int v = *p;
  cplus_free_registered(p, __FILE__, __LINE__);
  return v == 9 ? 0 : 1;
}
