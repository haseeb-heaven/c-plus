#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

void *cplus_malloc(size_t); void *cplus_calloc(size_t,size_t); void *cplus_realloc(void*,size_t); void cplus_free(void*);
void *cplus_memcpy(void*,const void*,size_t); void *cplus_memmove(void*,const void*,size_t); void *cplus_memset(void*,int,size_t); int cplus_memcmp(const void*,const void*,size_t);
size_t cplus_strlen(const char*); char *cplus_strcpy(char*,const char*); char *cplus_strncpy(char*,const char*,size_t); int cplus_strcmp(const char*,const char*);
FILE *cplus_fopen(const char*,const char*); int cplus_fclose(FILE*); size_t cplus_fwrite(const void*,size_t,size_t,FILE*); size_t cplus_fread(void*,size_t,size_t,FILE*); int cplus_remove(const char*);
int cplus_snprintf(char*,size_t,const char*,...);

static void t_alloc(void){
    for(int i=0;i<10;i++){ void*p=cplus_malloc(16+i); assert(p); memset(p,0xAB,16+i); cplus_free(p);} 
    void *z=cplus_calloc(4,4); assert(z); for(int i=0;i<16;i++) assert(((unsigned char*)z)[i]==0); cplus_free(z);
    void *r=cplus_malloc(8); assert(r); r=cplus_realloc(r,64); assert(r); cplus_free(r);
    cplus_free(NULL);
}
static void t_mem(void){
    char a[16],b[16];
    for(int i=0;i<10;i++){ memset(b,i,16); cplus_memcpy(a,b,16); assert(cplus_memcmp(a,b,16)==0); }
    char o[8]="abcd"; cplus_memmove(o+1,o,3); assert(memcmp(o,"aabc",4)==0);
    cplus_memset(a,0,16); for(int i=0;i<16;i++) assert(a[i]==0);
}
static void t_str(void){
    char d[32];
    for(int i=0;i<10;i++){ char s[8]; snprintf(s,8,"%d",i); cplus_strcpy(d,s); assert(cplus_strcmp(d,s)==0); assert(cplus_strlen(d)==strlen(s)); }
    cplus_strncpy(d,"abcdef",3); d[3]=0; assert(strcmp(d,"abc")==0);
}
static void t_stdio(void){
    for(int i=0;i<10;i++){
        char p[64]; cplus_snprintf(p,sizeof(p),"/tmp/cplus_libc10_%d.tmp",i);
        FILE*f=cplus_fopen(p,"wb+"); assert(f);
        assert(cplus_fwrite("ok",1,2,f)==2);
        rewind(f); char b[4]={0}; assert(cplus_fread(b,1,2,f)==2); assert(memcmp(b,"ok",2)==0);
        cplus_fclose(f); cplus_remove(p);
    }
}
int main(void){ t_alloc(); t_mem(); t_str(); t_stdio(); puts("libc_10_scenarios: ok"); return 0; }
