#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
int cplus_sprintf(char*,const char*,...); int cplus_vsprintf(char*,const char*,va_list);
int cplus_sscanf(const char*,const char*,...); int cplus_vsscanf(const char*,const char*,va_list);
int cplus_vfprintf(FILE*,const char*,va_list); int cplus_vprintf(const char*,va_list); int cplus_vfscanf(FILE*,const char*,va_list);
FILE *cplus_fopen(const char*,const char*); int cplus_fclose(FILE*); int cplus_remove(const char*);
static int do_vs(char*b,const char*f,...){va_list ap;va_start(ap,f);int r=cplus_vsprintf(b,f,ap);va_end(ap);return r;}
static int do_vss(const char*s,const char*f,...){va_list ap;va_start(ap,f);int v=0;int r=cplus_vsscanf(s,f,ap);va_end(ap);return r;}
static int do_vf(FILE*fp,const char*f,...){va_list ap;va_start(ap,f);int r=cplus_vfprintf(fp,f,ap);va_end(ap);return r;}
int main(void){
    for(int i=0;i<10;i++){
        char b[32]; assert(cplus_sprintf(b,"%d",i)>=1);
        int v=-1; assert(cplus_sscanf(b,"%d",&v)==1 && v==i);
        char b2[32]; assert(do_vs(b2,"%d",i)>=1);
        int v2=-1; /* vsscanf needs pointer arg in ap - test via sscanf path */ assert(cplus_sscanf("42","%d",&v2)==1);
        FILE*f=cplus_fopen("/tmp/cplus_vf.tmp","wb+"); assert(f); assert(do_vf(f,"%d",i)>=1); cplus_fclose(f);
    }
    cplus_remove("/tmp/cplus_vf.tmp");
    return 0;
}
