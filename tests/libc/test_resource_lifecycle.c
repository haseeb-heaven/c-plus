/* Resource/handle lifecycle stress: temp files, repeated open/close/failure,
 * alloc/free ownership patterns. Designed for normal + ASan/UBSan runs. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

void *cplus_malloc(size_t); void *cplus_calloc(size_t,size_t); void *cplus_realloc(void*,size_t); void cplus_free(void*);
FILE *cplus_fopen(const char*,const char*); int cplus_fclose(FILE*); size_t cplus_fread(void*,size_t,size_t,FILE*);
size_t cplus_fwrite(const void*,size_t,size_t,FILE*); int cplus_fseek(FILE*,long,int); long cplus_ftell(FILE*);
int cplus_fflush(FILE*); int cplus_remove(const char*); int cplus_rename(const char*,const char*);
char *cplus_fgets(char*,int,FILE*); int cplus_fputs(const char*,FILE*); int cplus_getc(FILE*); int cplus_putc(int,FILE*);
int cplus_feof(FILE*); int cplus_ferror(FILE*); void cplus_clearerr(FILE*);
int cplus_snprintf(char*,size_t,const char*,...);

static void alloc_lifecycle(void){
    for(int i=0;i<200;i++){
        size_t n=(size_t)(i%128)+1;
        unsigned char *p=(unsigned char*)cplus_malloc(n);
        assert(p);
        memset(p, (int)(i&0xff), n);
        assert(p[0]==(unsigned char)(i&0xff));
        p=(unsigned char*)cplus_realloc(p,n*2);
        assert(p);
        assert(p[0]==(unsigned char)(i&0xff));
        cplus_free(p);
    }
    for(int i=0;i<100;i++){
        size_t c=(size_t)(i%16)+1, s=(size_t)(i%32)+1;
        unsigned char *p=(unsigned char*)cplus_calloc(c,s);
        assert(p);
        for(size_t j=0;j<c*s;j++) assert(p[j]==0);
        cplus_free(p);
    }
    cplus_free(NULL);
}

static void file_lifecycle(void){
    for(int i=0;i<100;i++){
        char p[64], q[64];
        cplus_snprintf(p,sizeof(p),"/tmp/cplus_life_%d.tmp",i);
        cplus_snprintf(q,sizeof(q),"/tmp/cplus_life_%d.ren",i);
        FILE *f=cplus_fopen(p,"wb+");
        assert(f);
        assert(cplus_fputs("line\n",f)>=0);
        for(int k=0;k<10;k++) assert(cplus_putc('a'+(k%26),f)!=(-1));
        assert(cplus_fflush(f)==0);
        assert(cplus_fseek(f,0,0)==0);
        char buf[64]={0};
        assert(cplus_fgets(buf,sizeof(buf),f)!=NULL);
        assert(cplus_fseek(f,0,0)==0);
        int c=cplus_getc(f); assert(c=='l');
        (void)cplus_feof(f); (void)cplus_ferror(f); cplus_clearerr(f);
        assert(cplus_ftell(f)>=1);
        assert(cplus_fclose(f)==0);
        assert(cplus_rename(p,q)==0);
        f=cplus_fopen(q,"rb");
        assert(f);
        char rbuf[32]={0}; cplus_fread(rbuf,1,4,f);
        cplus_fclose(f);
        assert(cplus_remove(q)==0);
    }
    /* failure paths */
    assert(cplus_fopen("/no/such/cplus/path/file.tmp","rb")==NULL);
    assert(cplus_remove("/no/such/cplus/path/file.tmp")!=0);
}

int main(void){
    alloc_lifecycle();
    file_lifecycle();
    printf("resource_lifecycle: ok\n");
    return 0;
}
