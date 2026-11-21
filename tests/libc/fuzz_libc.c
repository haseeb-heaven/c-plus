/* Randomized fuzz/stress for the C+ libc slice (deterministic seed).
 * Exercises mem/string/math functions with in-bounds, defined inputs; asserts
 * no crash and basic invariants. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

void *cplus_memcpy(void*,const void*,size_t);
void *cplus_memmove(void*,const void*,size_t);
void *cplus_memset(void*,int,size_t);
int cplus_memcmp(const void*,const void*,size_t);
void *cplus_memchr(const void*,int,size_t);
size_t cplus_strlen(const char*);
char *cplus_strcpy(char*,const char*);
int cplus_strcmp(const char*,const char*);
char *cplus_strstr(const char*,const char*);
void *cplus_memmem(const void*,size_t,const void*,size_t);
char *cplus_strtok_r(char*,const char*,char**);
int cplus_strverscmp(const char*,const char*);
double cplus_sqrt(double);

static unsigned st=0x12345678u;
static unsigned R(void){ st=st*1664525u+1013904223u; return st; }

int main(void){
    for(int i=0;i<5000;i++){
        unsigned char a[64], b[64];
        for(int j=0;j<64;j++){ a[j]=(unsigned char)R(); b[j]=(unsigned char)R(); }
        size_t n=R()%65;
        (void)cplus_memcpy(a,b,n);
        (void)cplus_memmove(a,a+1,n<63?n:62);
        (void)cplus_memset(a,(int)R(),n);
        (void)cplus_memcmp(a,b,n);
        (void)cplus_memchr(a,'x',n);

        char s1[32], s2[32];
        for(int j=0;j<31;j++){ s1[j]=(char)('a'+(R()%26)); s2[j]=(char)('a'+(R()%26)); }
        s1[R()%32]=0; s2[R()%32]=0;
        (void)cplus_strlen(s1);
        (void)cplus_strcmp(s1,s2);
        (void)cplus_strverscmp(s1,s2);
        char dst[32]; cplus_strcpy(dst,s1);
        assert(strcmp(dst,s1)==0);
        (void)cplus_strstr(s1,"ab");
        (void)cplus_memmem(s1,strlen(s1),"ab",2);
        char tok[32]; strcpy(tok,s1); char *sp=NULL; (void)cplus_strtok_r(tok,",",&sp);
        (void)cplus_sqrt((double)(i%1000));
    }
    puts("fuzz_libc: ok");
    return 0;
}
