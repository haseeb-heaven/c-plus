extern void cplus_check_nonnull(const void *, const char *, int, const char *);
#if defined(__GNUC__) || defined(__clang__)
#define CPLUS_NONNULL(P) ({ __typeof__(P) __cplus_nn_p = (P); \
  cplus_check_nonnull((const void *)__cplus_nn_p, __FILE__, __LINE__, #P); \
  __cplus_nn_p; })
#else
#define CPLUS_NONNULL(P) \
  (cplus_check_nonnull((const void *)(P), __FILE__, __LINE__, #P), (P))
#endif
#include <stddef.h>
extern void *cplus_register_allocation(void *, size_t, const char *, int);
extern void cplus_free_registered(void *, const char *, int);
extern void *cplus_realloc_registered(void *, void *, size_t, const char *, int);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/libc_interop.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
  char buf[16];
  strcpy(buf, "core");
  int *p = cplus_register_allocation((malloc(sizeof(int))), 4, __FILE__, __LINE__);
  *CPLUS_NONNULL(p) = 7;
  int ok = strlen(buf) == 4 && *CPLUS_NONNULL(p) == 7;
  cplus_free_registered(p, __FILE__, __LINE__);
  return ok ? 0 : 1;
}
