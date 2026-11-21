#include <stdio.h>
#include "safe_alloc.h"
int main(void) {
    unsigned char *p = NULL;
    if (calloc_s((void**)&p, 4, 16) != CPLUS_SAFE_OK) return 1;
    for (int i=0;i<64;++i) if (p[i]!=0) return 1;
    free_s((void**)&p);
    puts("calloc_s: ok");
    return 0;
}
