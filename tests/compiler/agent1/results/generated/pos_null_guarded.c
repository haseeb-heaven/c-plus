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
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/null_guarded.c"
#include <stdlib.h>
#include <stdio.h>
struct Node { struct Node *next; int value; };
int main(void) {
    struct Node *next = NULL;
    struct Node *newchild = cplus_register_allocation((malloc(sizeof(*newchild))), 16, __FILE__, __LINE__);
    if (!newchild) return 1;
    next = newchild;
    if (next != NULL) {
        CPLUS_NONNULL(next)->next = newchild;
        CPLUS_NONNULL(next)->value = 7;
    }
    printf("%d\n", CPLUS_NONNULL(next)->value);
    cplus_free_registered(newchild, __FILE__, __LINE__);
    return 0;
}
