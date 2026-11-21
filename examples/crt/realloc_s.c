#include <stdio.h>
#include <string.h>
#include "safe_alloc.h"
int main(void) {
    char *p = NULL;
    if (malloc_s((void**)&p, 4) != CPLUS_SAFE_OK) return 1;
    memcpy(p, "abc", 4);
    if (realloc_s((void**)&p, 64) != CPLUS_SAFE_OK) return 1;
    if (strcmp(p, "abc") != 0) return 1;
    free_s((void**)&p);
    puts("realloc_s: ok");
    return 0;
}
