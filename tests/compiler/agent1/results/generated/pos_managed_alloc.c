extern void cplus_check_nonnull(const void *, const char *, int, const char *);
#if defined(__GNUC__) || defined(__clang__)
#define CPLUS_NONNULL(P) ({ __typeof__(P) __cplus_nn_p = (P); \
  cplus_check_nonnull((const void *)__cplus_nn_p, __FILE__, __LINE__, #P); \
  __cplus_nn_p; })
#else
#define CPLUS_NONNULL(P) \
  (cplus_check_nonnull((const void *)(P), __FILE__, __LINE__, #P), (P))
#endif
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/managed_alloc.c"
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"

typedef struct Buffer {
    int size;
    char name[16];
} Buffer;

int main(void) {
     Buffer *b = cplus_new(Buffer); 
    CPLUS_NONNULL(b)->size = 1024;
    strcpy(CPLUS_NONNULL(b)->name, "buffer");
    printf("%d %s\n", CPLUS_NONNULL(b)->size, CPLUS_NONNULL(b)->name);
    { __typeof__(0) __cplus_ret = (0); cplus_release(b); b = NULL;  return __cplus_ret;; }
cplus_release(b); b = NULL; 
#line 17 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/managed_alloc.c"
}
