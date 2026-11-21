#include <stdio.h>
#include "safe_alloc.h"
int main(void) {
    void *p = NULL;
    if (malloc_s(&p, 1024) != CPLUS_SAFE_OK) return 1;
    free_s(&p);
    puts(p == NULL ? "malloc_s: ok" : "malloc_s: fail");
    return p == NULL ? 0 : 1;
}
