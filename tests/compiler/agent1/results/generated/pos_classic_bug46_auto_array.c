extern void cplus_check_nonnull(const void *, const char *, int, const char *);
#if defined(__GNUC__) || defined(__clang__)
#define CPLUS_NONNULL(P) ({ __typeof__(P) __cplus_nn_p = (P); \
  cplus_check_nonnull((const void *)__cplus_nn_p, __FILE__, __LINE__, #P); \
  __cplus_nn_p; })
#else
#define CPLUS_NONNULL(P) \
  (cplus_check_nonnull((const void *)(P), __FILE__, __LINE__, #P), (P))
#endif
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/classic_bug46_auto_array.cp"
/* Bug 46: arrays decay with auto in C+ (fixed by pointer-type inference). */
#include "cplus/cplus.h"
int main(void) { int arr[4] = {1,2,3,4}; __typeof__(&arr[0]) p = arr; print(CPLUS_NONNULL(p)[0], CPLUS_NONNULL(p)[3]); return 0; }
