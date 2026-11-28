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
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/realloc_success_pattern.c"
#include <stdlib.h>
int main(void){ int *p=cplus_register_allocation((malloc(sizeof(int))), 4, __FILE__, __LINE__); void *tmp=cplus_realloc_registered((p), (realloc(p,2*sizeof(int))), 8, __FILE__, __LINE__); if(tmp!=NULL) p=tmp; if(!p) return 1; *p=7; cplus_free_registered(p, __FILE__, __LINE__); return 0; }
