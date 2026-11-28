#include <assert.h>
#include <stdio.h>
#include <string.h>
FILE *cplus_fopen(const char*,const char*); int cplus_fclose(FILE*);
size_t cplus_fwrite(const void*,size_t,size_t,FILE*); size_t cplus_fread(void*,size_t,size_t,FILE*);
int cplus_fseek(FILE*,long,int); long cplus_ftell(FILE*); int cplus_fflush(FILE*);
int cplus_remove(const char*); int cplus_rename(const char*,const char*);
char *cplus_fgets(char*,int,FILE*); int cplus_fputs(const char*,FILE*);
int cplus_getc(FILE*); int cplus_putc(int,FILE*); int cplus_feof(FILE*); int cplus_ferror(FILE*); void cplus_clearerr(FILE*);
int main(void){
    const char *p="/tmp/cplus_stdio2.tmp"; const char *p2="/tmp/cplus_stdio2_2.tmp";
    FILE *f=cplus_fopen(p,"wb+"); assert(f);
    assert(cplus_fputs("hello",f)>=0); assert(cplus_fflush(f)==0);
    assert(cplus_fseek(f,0,SEEK_SET)==0);
    char b[16]={0}; assert(cplus_fgets(b,sizeof(b),f)!=NULL); assert(strcmp(b,"hello")==0);
    assert(cplus_fseek(f,0,SEEK_END)==0); assert(cplus_ftell(f)>=5);
    assert(cplus_ferror(f)==0); cplus_clearerr(f);
    cplus_fclose(f);
    assert(cplus_rename(p,p2)==0); assert(cplus_remove(p2)==0);
    puts("stdio2: ok"); return 0;
}
