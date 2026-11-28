#if defined(CPLUS_AUTO_RUNTIME)
extern void cplus_set_auto_runtime(int);
static void __attribute__((constructor)) __cplus_auto_runtime_init(void) {
  cplus_set_auto_runtime(1);
}
#endif
#include <stddef.h>
extern void *cplus_register_allocation(void *, size_t, const char *, int);
extern void cplus_free_registered(void *, const char *, int);
extern void *cplus_realloc_registered(void *, void *, size_t, const char *, int);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/libccontract_null_path.c"
#include <stdlib.h>
int main(void) {
  int *p = cplus_register_allocation((malloc(sizeof(int))), 4, __FILE__, __LINE__);
  if (p == NULL) return 1;  // allocation failure: nothing to release
  cplus_free_registered(p, __FILE__, __LINE__);
  return 0;
}
