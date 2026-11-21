#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "safe_alloc.h"
int main(void) {
    void *p = NULL;
    assert(malloc_s(&p, 32) == CPLUS_SAFE_OK && p != NULL);
    free(p);
    assert(malloc_s(NULL, 32) == CPLUS_SAFE_EINVAL);
    p=NULL; assert(malloc_s(&p, 0) == CPLUS_SAFE_OK && p!=NULL); free(p);
    puts("unit malloc_s: ok");
    return 0;
}
