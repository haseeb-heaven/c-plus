/* Adversarial tests: attempt to "break" checked APIs with invalid/boundary inputs.
 * All cases must be defined behavior for the checked API (error return) or normal. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_alloc.h"
#include "safe_io.h"
#include "safe_format.h"
#include "safe_error.h"

int main(void) {
    char d[8]; char s[8]="abc";
    /* memcpy_s: NULLs, zero, overflow, exact boundary */
    assert(cplus_memcpy_s(NULL,8,s,3)!=0);
    assert(cplus_memcpy_s(d,8,NULL,3)!=0);
    assert(cplus_memcpy_s(d,0,s,0)!=0); /* dmax 0 invalid */
    assert(cplus_memcpy_s(d,2,s,3)!=0); /* too small */
    /* smax==0: Annex K implementation-defined/constraint; accept error or success but no crash */
    (void)cplus_memcpy_s(d,8,s,0);

    /* memmove_s overlap and invalid */
    char o[8]="abcd";
    assert(cplus_memmove_s(o+1,7,o,3)==0);
    assert(cplus_memmove_s(o,2,o,4)!=0);

    /* memset_s */
    assert(cplus_memset_s(NULL,8,0,1)!=0);
    { unsigned char b4= d[4]; (void)cplus_memset_s(d,4,1,5); assert(d[4]==b4); }

    /* string */
    assert(cplus_strcpy_s(d,8,"ok")==0);
    assert(cplus_strcpy_s(d,2,"toolong")!=0);
    assert(cplus_strcat_s(d,8,"!")==0);
    assert(cplus_strncat_s(d,8,"xxxx",1)==0);
    int ind=0; assert(cplus_strcmp_s("a",2,"b",&ind)==0 && ind<0);
    assert(cplus_strnlen_s(NULL,5)==0);
    assert(cplus_strnlen_s("abc",2)==2);

    /* alloc */
    void *p=NULL; assert(malloc_s(&p,16)==CPLUS_SAFE_OK);
    void *const old=p; assert(realloc_s(&p,(size_t)-1)==CPLUS_SAFE_ENOMEM); assert(p==old);
    free_s(&p); assert(p==NULL);
    free_s(&p);
    void *q=NULL; assert(calloc_s(&q,(size_t)-1,2)==CPLUS_SAFE_EOVERFLOW);

    /* io */
    FILE *f=NULL; assert(cplus_fopen_s(&f,"/tmp/cplus_adv.tmp","wb+")==CPLUS_SAFE_OK);
    assert(cplus_fwrite_s("abc",3,1,3,f)==3);
    rewind(f); char rb[4]={0}; assert(cplus_fread_s(rb,4,1,3,f)==3); assert(strcmp(rb,"abc")==0);
    assert(cplus_fclose_s(&f)==CPLUS_SAFE_OK);
    assert(cplus_fclose_s(&f)==CPLUS_SAFE_EINVAL);
    remove("/tmp/cplus_adv.tmp");

    /* format */
    char fb[8]; assert(cplus_snprintf_s(fb,sizeof(fb),"%s","hi")==2);
    assert(cplus_sprintf_s(fb,sizeof(fb),"%d",1)==1);
    puts("adversarial_break: ok");
    return 0;
}
