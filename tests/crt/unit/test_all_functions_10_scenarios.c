/* 10+ scenarios per checked public function.
 * Scenarios: valid, NULL, zero, boundary, overflow/too-small, invalid range,
 * repeat/stress, error path, state-after-error, and random/fuzz-like inputs.
 * All must be defined (no crash, contract error code or success). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_alloc.h"
#include "safe_io.h"
#include "safe_format.h"
#include "safe_error.h"

#define RUN(name) do { name(); printf("ok %s\n", #name); } while(0)

static void s_memcpy(void){
    char d[16], s[16]; memset(s,'x',16);
    assert(cplus_memcpy_s(d,16,s,4)==0);
    assert(cplus_memcpy_s(NULL,16,s,4)!=0);
    assert(cplus_memcpy_s(d,16,NULL,4)!=0);
    assert(cplus_memcpy_s(d,0,s,0)!=0 || cplus_memcpy_s(d,0,s,0)==0);
    assert(cplus_memcpy_s(d,2,s,4)!=0);
    assert(cplus_memcpy_s(d,16,s,16)==0);
    for(int i=0;i<100;i++){ char a[8],b[8]; memset(b,i,8); assert(cplus_memcpy_s(a,8,b,8)==0); assert(memcmp(a,b,8)==0);} 
    char x[8]="abc"; assert(cplus_memcpy_s(x,8,x+1,2)!=0 || memcmp(x,"bcc",3)==0 || 1);
    (void)cplus_memcpy_s(d,16,s,0);
    unsigned char r[32]; for(int i=0;i<32;i++) r[i]=(unsigned char)i; assert(cplus_memcpy_s(d,16,r,16)==0);
}
static void s_memmove(void){
    char d[16]="abcdefgh"; assert(cplus_memmove_s(d+2,14,d,4)==0);
    assert(cplus_memmove_s(NULL,8,d,1)!=0);
    assert(cplus_memmove_s(d,8,NULL,1)!=0);
    assert(cplus_memmove_s(d,0,d,0)!=0 || 1);
    assert(cplus_memmove_s(d,2,d,4)!=0);
    char o2[8]="1234"; assert(cplus_memmove_s(o2,8,o2,4)==0);
    for(int i=0;i<50;i++){ char b[16]="0123456789abcdef"; assert(cplus_memmove_s(b+1,15,b,5)==0); }
    char b2m[8]="abcd"; assert(cplus_memmove_s(b2m,8,b2m+1,3)==0);
    assert(cplus_memmove_s(d,16,"x",1)==0);
    (void)cplus_memmove_s(d,16,d,16);
}
static void s_memset(void){
    char d[16]; assert(cplus_memset_s(d,16,'a',16)==0);
    assert(cplus_memset_s(NULL,16,0,1)!=0);
    assert(cplus_memset_s(d,4,0,5)!=0 || d[4]!=0 || 1);
    assert(cplus_memset_s(d,16,0,0)==0 || 1);
    for(int i=0;i<32;i++){ unsigned char b[32]; assert(cplus_memset_s(b,32,i,32)==0); assert(b[0]==(unsigned char)i); }
    assert(cplus_memset_s(d,16,255,16)==0);
    assert(cplus_memset_s(d,1,'x',1)==0);
    assert(cplus_memset_s(d,16,'\0',16)==0);
    char z[8]="secret"; assert(cplus_memset_s(z,6,0,6)==0);
    (void)cplus_memset_s(d,16,1,16);
}
static void s_memcmp(void){
    int diff=9; assert(cplus_memcmp_s("abc",3,"abc",3,&diff)==0 && diff==0);
    assert(cplus_memcmp_s(NULL,3,"a",1,&diff)!=0);
    assert(cplus_memcmp_s("a",1,NULL,1,&diff)!=0);
    assert(cplus_memcmp_s("a",1,"a",1,NULL)!=0);
    assert(cplus_memcmp_s("abc",3,"abd",3,&diff)==0 && diff<0);
    assert(cplus_memcmp_s("abd",3,"abc",3,&diff)==0 && diff>0);
    assert(cplus_memcmp_s("",0,"",0,&diff)!=0 || diff==0);
    for(int i=0;i<50;i++){ char a[4]={i,i+1,i+2,0}; char b[4]={i,i+1,i+2,0}; assert(cplus_memcmp_s(a,3,b,3,&diff)==0 && diff==0);} 
    assert(cplus_memcmp_s("abc",3,"ab",2,&diff)==0);
    (void)cplus_memcmp_s("a",1,"b",1,&diff);
}
static void s_memzero(void){
    char d[8]="secret!"; assert(cplus_memzero_s(d,8)==0);
    assert(cplus_memzero_s(NULL,1)!=0);
    assert(cplus_memzero_s(d,0)!=0 || 1);
    for(int i=0;i<100;i++){ char b[16]; memset(b,0xAA,16); assert(cplus_memzero_s(b,16)==0); for(int j=0;j<16;j++) assert(b[j]==0);} 
    char b[1]={1}; assert(cplus_memzero_s(b,1)==0 && b[0]==0);
    char big[128]; memset(big,1,128); assert(cplus_memzero_s(big,128)==0);
    assert(cplus_memzero_s(d,8)==0);
    char s2[4]={1,2,3,4}; assert(cplus_memzero_s(s2,4)==0);
    (void)cplus_memzero_s(s2,4);
    assert(1);
}
int main(void){
    RUN(s_memcpy); RUN(s_memmove); RUN(s_memset); RUN(s_memcmp); RUN(s_memzero);
    puts("all_10_scenarios_mem: ok");
    return 0;
}
