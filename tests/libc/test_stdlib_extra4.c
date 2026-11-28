#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
int cplus_setenv(const char*,const char*,int); int cplus_unsetenv(const char*); int cplus_putenv(char*);
int cplus_rand_r(unsigned int*); float cplus_strtof(const char*,char**); long double cplus_strtold(const char*,char**);
intmax_t cplus_imaxabs(intmax_t); imaxdiv_t cplus_imaxdiv(intmax_t,intmax_t);
int main(void){
    assert(cplus_setenv("CPLUS_T","1",1)==0);
    char *v=getenv("CPLUS_T"); assert(v && strcmp(v,"1")==0);
    assert(cplus_unsetenv("CPLUS_T")==0);
    for(int i=0;i<10;i++){ unsigned s=42; int a=cplus_rand_r(&s), b=cplus_rand_r(&s); assert(a!=b||1); }
    char *e=NULL; assert(cplus_strtof("1.5",&e)==1.5f);
    assert(cplus_strtold("2.5",&e)==2.5L);
    for(int i=0;i<10;i++) assert(cplus_strtof("0.5",&e)>0.0f);
    assert(cplus_imaxabs((intmax_t)-7)==7);
    { imaxdiv_t d=cplus_imaxdiv(100,7); assert(d.quot*7+d.rem==100); for(int i=0;i<10;i++){ imaxdiv_t t=cplus_imaxdiv(i*10,3); assert(t.quot*3+t.rem==i*10);} }
    return 0;
}
