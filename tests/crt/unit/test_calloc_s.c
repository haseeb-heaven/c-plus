#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "safe_alloc.h"
int main(void) {
    void *p = NULL;
    assert(calloc_s(&p, 4, 4) == CPLUS_SAFE_OK);
    for(int i=0;i<16;i++) assert(((unsigned char*)p)[i]==0);
    free(p);
    p=NULL; assert(calloc_s(&p, (size_t)-1, 2) == CPLUS_SAFE_EOVERFLOW);
    assert(calloc_s(NULL,1,1) == CPLUS_SAFE_EINVAL);
    puts("unit calloc_s: ok");
    return 0;
}
