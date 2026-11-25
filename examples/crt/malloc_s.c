#include <stdio.h>
#include "safe_alloc.h"
int main(void) {
    void *p = malloc_s(1024);
    if (p == NULL) return 1;
    free_s(p);
    puts("malloc_s: ok");
    return 0;
}
