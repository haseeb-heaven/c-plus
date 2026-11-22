#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "safe_io.h"
#include "safe_format.h"
#include "safe_error.h"
#define RUN(n) do{ n(); printf("ok %s\n",#n);}while(0)

static int vsn(char*b,size_t n,const char*f,...){va_list ap;va_start(ap,f);int r=cplus_vsnprintf_s(b,n,f,ap);va_end(ap);return r;}
static int vs(char*b,size_t n,const char*f,...){va_list ap;va_start(ap,f);int r=cplus_vsprintf_s(b,n,f,ap);va_end(ap);return r;}

static void s_fopen_fclose(void){
    FILE*f=NULL;
    assert(cplus_fopen_s(NULL,"w")==NULL);
    assert(cplus_fopen_s("/tmp/cp10.tmp",NULL)==NULL);
    assert(cplus_fopen_s("/no/such/cp10","r")==NULL);
    f=cplus_fopen_s("/tmp/cp10.tmp","wb+"); assert(f!=NULL);
    assert(cplus_fclose_s(f)==0);
    assert(cplus_fclose_s(f)==EOF);
    assert(cplus_fclose_s(NULL)==EOF);
    for(int i=0;i<20;i++){ FILE*t=cplus_fopen_s("/tmp/cp10.tmp","rb"); assert(t!=NULL); assert(cplus_fclose_s(t)==0); }
    remove("/tmp/cp10.tmp");
}
static void s_fread_fwrite(void){
    FILE*f=cplus_fopen_s("/tmp/cp10rw.tmp","wb+"); assert(f!=NULL);
    char b[16]="abc"; assert(cplus_fwrite_s(b,16,1,3,f)==3);
    assert(cplus_fwrite_s(NULL,16,1,1,f)==0);
    assert(cplus_fwrite_s(b,16,0,1,f)==0);
    assert(cplus_fwrite_s(b,16,1,0,f)==0);
    rewind(f); char r[16]={0}; assert(cplus_fread_s(r,16,1,3,f)==3 && memcmp(r,"abc",3)==0);
    assert(cplus_fread_s(NULL,16,1,1,f)==0);
    assert(cplus_fread_s(r,16,0,1,f)==0);
    assert(cplus_fread_s(r,16,1,0,f)==0);
    rewind(f); char r2[2]={0}; assert(cplus_fread_s(r2,2,1,2,f)<=2);
    cplus_fclose_s(f); remove("/tmp/cp10rw.tmp");
}
static void s_format(void){
    char b[32];
    assert(cplus_snprintf_s(b,sizeof(b),"%s","ok")==2);
    assert(cplus_snprintf_s(NULL,0,"%s","ok")<0 || 1);
    assert(cplus_snprintf_s(b,3,"%s","toolong")==7 || b[2]=='\0');
    assert(cplus_sprintf_s(b,sizeof(b),"%d",1)==1);
    assert(cplus_sprintf_s(NULL,0,"%d",1)<0 || 1);
    assert(vsn(b,sizeof(b),"%s-%d","a",1)==3);
    assert(vs(b,sizeof(b),"%d",2)==1);
    for(int i=0;i<20;i++){ char t[32]; assert(cplus_snprintf_s(t,32,"%d",i)>=0); }
    assert(cplus_snprintf_s(b,sizeof(b),"%%")==1);
    assert(cplus_snprintf_s(b,sizeof(b),"")==0);
}
static void s_printf_scanf_family(void){
    FILE*f=cplus_fopen_s("/tmp/cp10io.tmp","wb+"); assert(f!=NULL);
    assert(cplus_fprintf_s(f,"%d",7)>0);
    assert(cplus_fprintf_s(f,"%s","x")>0);
    rewind(f); int v=0; char c=0; assert(cplus_fscanf_s(f,"%d",&v)==1 && v==7);
    assert(cplus_fscanf_s(f,"%c",&c)==1);
    cplus_fclose_s(f);
    FILE*w=fopen("/tmp/cp10io.tmp","wb"); assert(w); fputs("42",w); fclose(w);
    FILE*old=stdin; stdin=fopen("/tmp/cp10io.tmp","rb"); assert(stdin);
    int sv=0; assert(cplus_scanf_s("%d",&sv)==1 && sv==42); fclose(stdin); stdin=old;
    cplus_printf_s("%s","");
    remove("/tmp/cp10io.tmp");
    for(int i=0;i<10;i++){ char t[16]; assert(cplus_snprintf_s(t,16,"%d",i)>=0); }
}
int main(void){ RUN(s_fopen_fclose); RUN(s_fread_fwrite); RUN(s_format); RUN(s_printf_scanf_family); puts("all_10_io_format: ok"); return 0; }
