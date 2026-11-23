#include <stddef.h>
extern void *cplus_register_allocation(void *, size_t, const char *, int);
extern void cplus_free_registered(void *, const char *, int);
extern void *cplus_realloc_registered(void *, void *, size_t, const char *, int);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/classic_bug23_fam_safe.c"
/* Bug 23: safe alternative — copy the whole allocation explicitly. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct Packet { int length; char payload[]; };
int main(void) {
    size_t total = sizeof(struct Packet) + 4;
    struct Packet *p1 = cplus_register_allocation((malloc(total)), 8, __FILE__, __LINE__);
    p1->length = 4;
    memcpy(p1->payload, "DATA", 4);
    struct Packet *p2 = cplus_register_allocation((malloc(total)), 8, __FILE__, __LINE__);
    memcpy(p2, p1, total);
    printf("%.*s\n", p2->length, p2->payload);
    cplus_free_registered(p1, __FILE__, __LINE__); cplus_free_registered(p2, __FILE__, __LINE__);
    return 0;
}
