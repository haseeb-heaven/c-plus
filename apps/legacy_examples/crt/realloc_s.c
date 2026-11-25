#include <stdio.h>
#include <string.h>
#include "safe_alloc.h"
int main(void) {
    char *p = (char *)malloc_s(4);
    if (p == NULL) return 1;
    memcpy(p, "abc", 4);
    char *q = (char *)realloc_s(p, 64);
    if (q == NULL) { free_s(p); return 1; }
    p = q;
    if (strcmp(p, "abc") != 0) { free_s(p); return 1; }
    free_s(p);
    puts("realloc_s: ok");
    return 0;
}
