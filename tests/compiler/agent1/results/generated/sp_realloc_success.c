#if defined(CPLUS_AUTO_RUNTIME)
extern void cplus_set_auto_runtime(int);
static void __attribute__((constructor)) __cplus_auto_runtime_init(void) {
  cplus_set_auto_runtime(1);
}
#endif
extern void cplus_check_nonnull(const void *, const char *, int, const char *);
#if defined(__GNUC__) || defined(__clang__)
#define CPLUS_NONNULL(P) ({ __typeof__(P) __cplus_nn_p = (P); \
  cplus_check_nonnull((const void *)__cplus_nn_p, __FILE__, __LINE__, #P); \
  __cplus_nn_p; })
#else
#define CPLUS_NONNULL(P) \
  (cplus_check_nonnull((const void *)(P), __FILE__, __LINE__, #P), (P))
#endif
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/realloc_success.c"
#include "cplus/cplus.h"
int main(void){int*p=cplus_realloc(NULL,sizeof(int));if(!p)return 1;*p=9;int v=*CPLUS_NONNULL(p);cplus_release(p);return v==9?0:1;}
