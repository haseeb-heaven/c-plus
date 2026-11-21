#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "safe_format.h"
#include "safe_io.h"
#include "safe_error.h"
static int wrap(char *b,size_t n,const char *fmt,...){va_list ap;va_start(ap,fmt);int r=cplus_vsnprintf_s(b,n,fmt,ap);va_end(ap);return r;}
int main(void){
    char b[16];
    assert(cplus_snprintf_s(b,sizeof(b),"%d",42)==2);
    assert(strcmp(b,"42")==0);
    assert(wrap(b,sizeof(b),"%s","x")==1);
    FILE *f=NULL;
    assert(cplus_fopen_s(&f,"/tmp/cplus_safe_fmt.tmp","wb+")==CPLUS_SAFE_OK);
    assert(cplus_fprintf_s(f,"%s %d","hi",3)>0);
    rewind(f);
    char s[8]={0}; int v=0;
    assert(cplus_fscanf_s(f,"%7s %d",s,&v)==2);
    assert(strcmp(s,"hi")==0 && v==3);
    cplus_fclose_s(&f);
    remove("/tmp/cplus_safe_fmt.tmp");
    puts("test_safe_format: ok");
    return 0;
}
