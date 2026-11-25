#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

FILE *cplus_fopen(const char*,const char*); int cplus_fclose(FILE*);
size_t cplus_fread(void*,size_t,size_t,FILE*); size_t cplus_fwrite(const void*,size_t,size_t,FILE*);
int cplus_vsnprintf(char*,size_t,const char*,__builtin_va_list);
int cplus_snprintf(char*,size_t,const char*,...);
int cplus_printf(const char*,...); int cplus_fprintf(FILE*,const char*,...);
int cplus_scanf(const char*,...); int cplus_fscanf(FILE*,const char*,...);
int cplus_remove(const char*);

static int wrap_vsn(char *b,size_t n,const char *fmt,...){__builtin_va_list ap; __builtin_va_start(ap,fmt); int r=cplus_vsnprintf(b,n,fmt,ap); __builtin_va_end(ap); return r;}

int main(void){
    char b[64];
    assert(cplus_snprintf(b,sizeof(b),"%s", "x")==1);
    assert(wrap_vsn(b,sizeof(b),"%d", 7)==1);
    cplus_printf("%s", "");
    const char *p="/tmp/cplus_stdio_cov.tmp";
    FILE *f=cplus_fopen(p,"wb+"); assert(f);
    cplus_fprintf(f,"%d", 42); fflush(f); fseek(f,0,SEEK_SET);
    int v=0; assert(cplus_fscanf(f,"%d",&v)==1 && v==42);
    cplus_fclose(f);
    /* scanf via stdin redirect */
    f=cplus_fopen(p,"wb"); assert(f); cplus_fprintf(f,"99"); cplus_fclose(f);
    FILE *old=stdin; stdin=fopen(p,"rb"); assert(stdin);
    int sv=0; assert(cplus_scanf("%d",&sv)==1 && sv==99);
    fclose(stdin); stdin=old;
    cplus_remove(p);
    return 0;
}
