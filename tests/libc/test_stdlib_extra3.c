/* Platform-backed stdlib conversions (C+ libc slice). */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
long long cplus_strtoll(const char*,char**,int);
unsigned long long cplus_strtoull(const char*,char**,int);
double cplus_strtod(const char*,char**);
double cplus_atof(const char*);
int main(void){
    char *e=NULL;
    assert(cplus_strtoll("12345",&e,10)==12345LL && e && *e==0);
    assert(cplus_strtoll("-9000000000",&e,10)==-9000000000LL);
    for(int i=0;i<10;i++){ char b[32]; snprintf(b,sizeof b,"%d",i*100); assert(cplus_strtoll(b,&e,10)==(long long)i*100); }
    assert(cplus_strtoull("18446744073709551615",&e,10)==18446744073709551615ULL);
    for(int i=0;i<10;i++){ assert(cplus_strtoull("42",&e,10)==42ULL); }
    assert(cplus_strtod("3.5",&e)==3.5 && e && *e==0);
    for(int i=0;i<10;i++) assert(cplus_strtod("2.5",&e)==2.5);
    assert(cplus_atof("1.25")==1.25);
    for(int i=0;i<10;i++) assert(cplus_atof("0.5")==0.5);
    return 0;
}
