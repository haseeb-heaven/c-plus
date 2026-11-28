/* Exhaustive adversarial/break tests for checked CRT APIs.
 * Goal: exercise invalid, boundary, overflow, truncation, EOF, and error paths.
 * All behavior must be defined by the checked API contract (error return, no crash). */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_alloc.h"
#include "safe_io.h"
#include "safe_format.h"
#include "safe_error.h"

static void test_mem(void){
    char d[16], s[16];
    memset(d,'A',sizeof(d)); memset(s,'B',sizeof(s));
    assert(cplus_memcpy_s(NULL,16,s,4)!=0);
    assert(cplus_memcpy_s(d,16,NULL,4)!=0);
    assert(cplus_memcpy_s(d,16,s,(rsize_t)RSIZE_MAX_MEM+1)!=0);
    assert(cplus_memcpy_s(d,2,s,4)!=0);
    assert(cplus_memcpy_s(d,16,s,4)==0 && memcmp(d,s,4)==0);
    /* overlap is undefined for memcpy_s in some impls; memmove must handle */
    char o[8]="abcd"; assert(cplus_memmove_s(o+1,7,o,4)==0);
    assert(cplus_memmove_s(NULL,8,o,1)!=0);
    assert(cplus_memmove_s(o,8,NULL,1)!=0);
    assert(cplus_memmove_s(o,1,o,2)!=0);
    assert(cplus_memset_s(NULL,1,0,1)!=0);
    assert(cplus_memset_s(d,16,0x3C,16)==0);
    int diff=0; assert(cplus_memcmp_s("abc",3,"abc",3,&diff)==0 && diff==0);
    assert(cplus_memcmp_s("abc",3,"abd",3,&diff)==0 && diff<0);
    assert(cplus_memcmp_s(NULL,3,"a",1,&diff)!=0);
    assert(cplus_memcmp_s("a",1,"a",1,NULL)!=0);
    char z[8]="secret!"; assert(cplus_memzero_s(z,8)==0); for(int i=0;i<8;i++) assert(z[i]==0);
    assert(cplus_memzero_s(NULL,1)!=0);
}

static void test_str(void){
    char d[16];
    assert(cplus_strcpy_s(d,sizeof(d),"hi")==0);
    assert(cplus_strcpy_s(NULL,8,"x")!=0);
    assert(cplus_strcpy_s(d,0,"x")!=0);
    assert(cplus_strcpy_s(d,2,"long")!=0);
    assert(cplus_strncpy_s(d,sizeof(d),"abcdef",3)==0 && strcmp(d,"abc")==0);
    assert(cplus_strncpy_s(d,2,"abcdef",3)!=0);
    strcpy(d,"a"); assert(cplus_strcat_s(d,sizeof(d),"b")==0 && strcmp(d,"ab")==0);
    assert(cplus_strcat_s(d,2,"bbb")!=0);
    strcpy(d,"a"); assert(cplus_strncat_s(d,sizeof(d),"bcdef",2)==0 && strcmp(d,"abc")==0);
    int ind=99; assert(cplus_strcmp_s("a",2,"a",&ind)==0 && ind==0);
    assert(cplus_strcmp_s("a",2,"b",&ind)==0 && ind<0);
    assert(cplus_strcmp_s(NULL,2,"b",&ind)!=0);
    assert(cplus_strnlen_s("abc",10)==3);
    assert(cplus_strnlen_s("abc",2)==2);
    assert(cplus_strnlen_s(NULL,10)==0);
}

static void test_alloc(void){
    errno = 0;
    assert(malloc_s((size_t)-1)==NULL && errno==EINVAL);
    void *p=malloc_s(0); assert(p!=NULL); free_s(p);
    p=malloc_s(32); assert(p!=NULL); memset(p,7,32);
    errno = 0;
    assert(realloc_s(p,(size_t)-1)==NULL && errno==EINVAL);
    assert(cplus_safe_ptr_is_live(p));
    p=realloc_s(p,64); assert(p!=NULL);
    assert(free_checked_s(p)==CPLUS_SAFE_OK);
    assert(free_checked_s(p)==CPLUS_SAFE_EINVAL);
    free_s(NULL);
    errno = 0;
    assert(calloc_s((size_t)-1,2)==NULL && errno==EOVERFLOW);
    void *q=calloc_s(4,4); assert(q!=NULL); for(int i=0;i<16;i++) assert(((unsigned char*)q)[i]==0); free_s(q);
}

static void test_io(void){
    FILE *f=NULL;
    errno=0; assert(cplus_fopen_s(NULL,"wb")==NULL && errno==EINVAL);
    errno=0; assert(cplus_fopen_s("/tmp/cplus_exhaust.tmp",NULL)==NULL && errno==EINVAL);
    assert(cplus_fopen_s("/no/such/path/cplus","rb")==NULL);
    f=cplus_fopen_s("/tmp/cplus_exhaust.tmp","wb+"); assert(f!=NULL);
    assert(cplus_fwrite_s("abc",3,1,3,f)==3);
    assert(cplus_fwrite_s(NULL,3,1,3,f)==0);
    rewind(f);
    char b[8]={0}; assert(cplus_fread_s(b,8,1,3,f)==3); assert(memcmp(b,"abc",3)==0);
    assert(cplus_fread_s(NULL,8,1,3,f)==0);
    assert(cplus_fclose_s(f)==0);
    assert(cplus_fclose_s(f)==EOF);
    remove("/tmp/cplus_exhaust.tmp");
}

static void test_format(void){
    char b[16];
    assert(cplus_snprintf_s(b,sizeof(b),"%s","ok")==2 && strcmp(b,"ok")==0);
    assert(cplus_snprintf_s(b,3,"%s","abcd")==4 && b[2]=='\0');
    assert(cplus_sprintf_s(b,sizeof(b),"%d", 12)==2);

}

static int vsprintf_wrap(char *b, size_t n, const char *fmt, ...) {
    va_list ap; va_start(ap,fmt); int r=cplus_vsnprintf_s(b,n,fmt,ap); va_end(ap); return r;
}

static void test_format2(void){
    char b[16];
    assert(vsprintf_wrap(b,sizeof(b),"%s-%d","a",1)==3);
    assert(strcmp(b,"a-1")==0);
}

int main(void){
    test_mem(); test_str(); test_alloc(); test_io(); test_format(); test_format2();
    puts("adversarial_exhaustive: ok");
    return 0;
}
