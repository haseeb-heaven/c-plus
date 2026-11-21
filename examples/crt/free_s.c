#include <stdio.h>
#include "safe_alloc.h"
int main(void) {
    void *p = NULL;
    if (malloc_s(&p, 8) != CPLUS_SAFE_OK) return 1;
    free_s(&p);
    free_s(&p); /* no-op */
    puts(p == NULL ? "free_s: ok" : "free_s: fail");
    return p == NULL ? 0 : 1;
}
