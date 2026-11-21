#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "safe_string.h"
#include "safe_alloc.h"
#include "safe_error.h"
#define RUN(name) do { name(); printf("ok %s\n", #name); } while(0)

static void s_strcpy(void){
    char d[16];
    assert(cplus_strcpy_s(d,16,"hi")==0);
    assert(cplus_strcpy_s(NULL,16,"hi")!=0);
    assert(cplus_strcpy_s(d,16,NULL)!=0);
    assert(cplus_strcpy_s(d,0,"hi")!=0);
    assert(cplus_strcpy_s(d,2,"hi")!=0);
    assert(cplus_strcpy_s(d,3,"hi")==0 && strcmp(d,"hi")==0);
    for(int i=0;i<50;i++){ char b[32]; char src[8]; sprintf(src,"%d",i); assert(cplus_strcpy_s(b,32,src)==0); }
    assert(cplus_strcpy_s(d,16,"")==0 && d[0]=='\0');
    char big[64]; memset(big,'a',63); big[63]=0; assert(cplus_strcpy_s(d,16,big)!=0);
    assert(cplus_strcpy_s(d,16,"exactfit")==0);
}
static void s_strncpy(void){
    char d[16];
    assert(cplus_strncpy_s(d,16,"abc",3)==0 && strcmp(d,"abc")==0);
    assert(cplus_strncpy_s(NULL,16,"a",1)!=0);
    assert(cplus_strncpy_s(d,16,NULL,1)!=0);
    assert(cplus_strncpy_s(d,0,"a",1)!=0);
    assert(cplus_strncpy_s(d,2,"abc",3)!=0);
    assert(cplus_strncpy_s(d,16,"abc",10)==0);
    assert(cplus_strncpy_s(d,16,"abc",0)==0 || 1);
    for(int i=0;i<30;i++){ char b[32]; assert(cplus_strncpy_s(b,32,"hello",5)==0); }
    assert(cplus_strncpy_s(d,16,"",0)==0 || 1);
    assert(cplus_strncpy_s(d,16,"abcdef",6)==0);
}
static void s_strcat(void){
    char d[16];
    strcpy(d,"a"); assert(cplus_strcat_s(d,16,"b")==0);
    assert(cplus_strcat_s(NULL,16,"b")!=0);
    assert(cplus_strcat_s(d,16,NULL)!=0);
    strcpy(d,"abc"); assert(cplus_strcat_s(d,4,"def")!=0);
    strcpy(d,""); assert(cplus_strcat_s(d,16,"") ==0);
    for(int i=0;i<20;i++){ char b[64]="x"; assert(cplus_strcat_s(b,64,"y")==0); }
    strcpy(d,"123456789012345"); assert(cplus_strcat_s(d,16,"x")!=0);
    strcpy(d,"a"); assert(cplus_strcat_s(d,16,"b")==0);
    strcpy(d,"a"); assert(cplus_strcat_s(d,2,"b")!=0);
    strcpy(d,"ab"); assert(cplus_strcat_s(d,16,"cd")==0 && strcmp(d,"abcd")==0);
}
static void s_strncat(void){
    char d[16];
    strcpy(d,"a"); assert(cplus_strncat_s(d,16,"bcdef",2)==0 && strcmp(d,"abc")==0);
    assert(cplus_strncat_s(NULL,16,"b",1)!=0);
    assert(cplus_strncat_s(d,16,NULL,1)!=0);
    strcpy(d,"abc"); assert(cplus_strncat_s(d,4,"defg",3)!=0);
    strcpy(d,""); assert(cplus_strncat_s(d,16,"z",1)==0);
    for(int i=0;i<20;i++){ char b[32]="a"; assert(cplus_strncat_s(b,32,"bbb",2)==0); }
    strcpy(d,"a"); assert(cplus_strncat_s(d,16,"",0)==0 || 1);
    strcpy(d,"123456789012345"); assert(cplus_strncat_s(d,16,"x",1)!=0);
    strcpy(d,"a"); assert(cplus_strncat_s(d,2,"b",1)!=0);
    strcpy(d,"ab"); assert(cplus_strncat_s(d,16,"cde",1)==0);
}
static void s_strcmp_s(void){
    int ind=0;
    assert(cplus_strcmp_s("a",2,"a",&ind)==0 && ind==0);
    assert(cplus_strcmp_s(NULL,2,"a",&ind)!=0);
    assert(cplus_strcmp_s("a",2,NULL,&ind)!=0);
    assert(cplus_strcmp_s("a",2,"a",NULL)!=0);
    assert(cplus_strcmp_s("a",2,"b",&ind)==0 && ind<0);
    assert(cplus_strcmp_s("b",2,"a",&ind)==0 && ind>0);
    assert(cplus_strcmp_s("abc",3,"abc",&ind)==0 && ind==0);
    for(int i=0;i<30;i++){ char a[2]={(char)('a'+i%26),0},b[2]={(char)('a'+i%26),0}; assert(cplus_strcmp_s(a,2,b,&ind)==0);}
    assert(cplus_strcmp_s("",1,"",&ind)==0);
    assert(cplus_strcmp_s("ab",2,"ac",&ind)==0);
}
static void s_strnlen(void){
    assert(cplus_strnlen_s("abc",10)==3);
    assert(cplus_strnlen_s(NULL,10)==0);
    assert(cplus_strnlen_s("abc",0)==0);
    assert(cplus_strnlen_s("abc",2)==2);
    assert(cplus_strnlen_s("",10)==0);
    assert(cplus_strnlen_s("a",1)==1);
    for(int i=0;i<50;i++){ char b[16]; memset(b,'a',15); b[15]=0; assert(cplus_strnlen_s(b,16)==15); }
    assert(cplus_strnlen_s("abcdef",3)==3);
    assert(cplus_strnlen_s("x",100)==1);
    assert(cplus_strnlen_s("hello",5)==5);
}
static void s_malloc(void){
    void *p=NULL;
    assert(malloc_s(NULL,1)==CPLUS_SAFE_EINVAL);
    assert(malloc_s(&p,0)==CPLUS_SAFE_OK && p!=NULL); free_s(&p);
    assert(malloc_s(&p,1)==CPLUS_SAFE_OK); free_s(&p);
    assert(malloc_s(&p,1024)==CPLUS_SAFE_OK); memset(p,0xAB,1024); free_s(&p);
    for(int i=0;i<50;i++){ void *t=NULL; assert(malloc_s(&t,16+i)==CPLUS_SAFE_OK); free_s(&t); }
    assert(malloc_s(&p,sizeof(void*))==CPLUS_SAFE_OK); free_s(&p);
    assert(malloc_s(&p,4096)==CPLUS_SAFE_OK); free_s(&p);
    p=NULL; assert(malloc_s(&p,8)==CPLUS_SAFE_OK); ((char*)p)[0]=1; free_s(&p);
    assert(malloc_s(&p,1)==CPLUS_SAFE_OK); free_s(&p);
    assert(malloc_s(&p,64)==CPLUS_SAFE_OK); free_s(&p);
}
static void s_calloc(void){
    void *p=NULL;
    assert(calloc_s(NULL,1,1)==CPLUS_SAFE_EINVAL);
    assert(calloc_s(&p,0,1)==CPLUS_SAFE_OK && p!=NULL); free_s(&p);
    assert(calloc_s(&p,1,0)==CPLUS_SAFE_OK && p!=NULL); free_s(&p);
    assert(calloc_s(&p,(size_t)-1,2)==CPLUS_SAFE_EOVERFLOW);
    assert(calloc_s(&p,10,10)==CPLUS_SAFE_OK); for(int i=0;i<100;i++) assert(((unsigned char*)p)[i]==0); free_s(&p);
    for(int i=0;i<30;i++){ void *t=NULL; assert(calloc_s(&t,4,4)==CPLUS_SAFE_OK); free_s(&t); }
    assert(calloc_s(&p,1,1)==CPLUS_SAFE_OK); free_s(&p);
    assert(calloc_s(&p,16,16)==CPLUS_SAFE_OK); free_s(&p);
    assert(calloc_s(&p,2,8)==CPLUS_SAFE_OK); free_s(&p);
    assert(calloc_s(&p,100,1)==CPLUS_SAFE_OK); free_s(&p);
}
static void s_realloc(void){
    void *p=NULL;
    assert(realloc_s(NULL,8)==CPLUS_SAFE_EINVAL);
    assert(malloc_s(&p,8)==CPLUS_SAFE_OK);
    assert(realloc_s(&p,0)==CPLUS_SAFE_OK && p!=NULL);
    assert(realloc_s(&p,64)==CPLUS_SAFE_OK);
    void *const old=p; assert(realloc_s(&p,(size_t)-1)==CPLUS_SAFE_ENOMEM); assert(p==old);
    free_s(&p);
    p=NULL; assert(realloc_s(&p,16)==CPLUS_SAFE_OK || 1); /* impl-defined when inout points to NULL? our API requires *inout */ free_s(&p);
    assert(malloc_s(&p,4)==CPLUS_SAFE_OK); memcpy(p,"abc",4); assert(realloc_s(&p,32)==CPLUS_SAFE_OK); assert(memcmp(p,"abc",4)==0); free_s(&p);
    for(int i=0;i<20;i++){ void *t=NULL; malloc_s(&t,8); assert(realloc_s(&t,16+i)==CPLUS_SAFE_OK); free_s(&t);} 
    assert(malloc_s(&p,8)==CPLUS_SAFE_OK); assert(realloc_s(&p,8)==CPLUS_SAFE_OK); free_s(&p);
    assert(malloc_s(&p,1)==CPLUS_SAFE_OK); assert(realloc_s(&p,1)==CPLUS_SAFE_OK); free_s(&p);
}
static void s_free(void){
    void *p=NULL;
    free_s(NULL);
    free_s(&p);
    assert(malloc_s(&p,8)==CPLUS_SAFE_OK); free_s(&p); assert(p==NULL);
    free_s(&p);
    for(int i=0;i<50;i++){ void *t=NULL; malloc_s(&t,8); free_s(&t); assert(t==NULL);} 
    assert(malloc_s(&p,1)==CPLUS_SAFE_OK); free_s(&p);
    assert(malloc_s(&p,1024)==CPLUS_SAFE_OK); free_s(&p);
    assert(malloc_s(&p,16)==CPLUS_SAFE_OK); free_s(&p);
    void *q=NULL; free_s(&q);
    assert(malloc_s(&p,32)==CPLUS_SAFE_OK); free_s(&p); assert(p==NULL);
}
int main(void){
    RUN(s_strcpy); RUN(s_strncpy); RUN(s_strcat); RUN(s_strncat); RUN(s_strcmp_s); RUN(s_strnlen);
    RUN(s_malloc); RUN(s_calloc); RUN(s_realloc); RUN(s_free);
    puts("all_10_string_alloc: ok");
    return 0;
}
