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
        next->next = newchild;
        next->value = 7;
    }
    printf("%d\n", next->value);
    cplus_free_registered(newchild, __FILE__, __LINE__);
    return 0;
}