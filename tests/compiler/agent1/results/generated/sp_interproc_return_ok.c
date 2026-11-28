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
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/interproc_return_ok.c"
#include <stdlib.h>
int *f(void){ int *tmp=cplus_register_allocation((malloc(sizeof(int))), 4, __FILE__, __LINE__); if(!tmp)return 0; cplus_free_registered(tmp, __FILE__, __LINE__); int *p=cplus_register_allocation((malloc(sizeof(int))), 4, __FILE__, __LINE__); return p; }
int main(void){ int *q=f(); if(!q)return 1; *q=3; cplus_free_registered(q, __FILE__, __LINE__); return 0; }
