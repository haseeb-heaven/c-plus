#include <stdio.h>
#include "safe_alloc.h"
int main(void) {
    void *p = malloc_s(8);
    if (p == NULL) return 1;
    free_s(p);
    free_s(p); /* double free safely refused */
    puts("free_s: ok");
    return 0;
}
