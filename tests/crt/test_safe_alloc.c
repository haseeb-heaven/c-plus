#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "safe_alloc.h"
int main(void){
    void *p=NULL;
    assert(malloc_s(&p,32)==CPLUS_SAFE_OK && p!=NULL);
    memset(p,7,32);
    assert(realloc_s(&p,64)==CPLUS_SAFE_OK && p!=NULL);
    assert(((unsigned char*)p)[0]==7);
    free_s(&p); assert(p==NULL);
    free_s(&p); /* no-op */
    assert(malloc_s(NULL,1)==CPLUS_SAFE_EINVAL);
    void *q=NULL;
    assert(calloc_s(&q,4,8)==CPLUS_SAFE_OK && q!=NULL);
    for(int i=0;i<32;i++) assert(((unsigned char*)q)[i]==0);
    /* realloc failure preserves owner */
    void *old=q;
    int rc=realloc_s(&q,(size_t)-1);
    assert(rc==CPLUS_SAFE_ENOMEM);
    assert(q==old);
    free_s(&q);
    /* calloc overflow */
    void *o=NULL;
    assert(calloc_s(&o,(size_t)-1,2)==CPLUS_SAFE_EOVERFLOW);
    puts("test_safe_alloc: ok");
    return 0;
}
