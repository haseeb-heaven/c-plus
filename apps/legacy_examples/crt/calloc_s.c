#include <stdio.h>
#include "safe_alloc.h"
int main(void) {
    unsigned char *p = (unsigned char *)calloc_s(4, 16);
    if (p == NULL) return 1;
    for (int i = 0; i < 64; ++i) if (p[i] != 0) return 1;
    free_s(p);
    puts("calloc_s: ok");
    return 0;
}
