/* Covers the platform-backed stdlib wrappers added in batch 5
 * (ecvt/fcvt/gcvt and the drand48 pseudo-random family). */
#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdlib.h>
#include <string.h>
char *cplus_ecvt(double,int,int*,int*); char *cplus_fcvt(double,int,int*,int*); char *cplus_gcvt(double,int,char*);
double cplus_drand48(void); double cplus_erand48(unsigned short[3]); unsigned short *cplus_seed48(unsigned short[3]); long cplus_lrand48(void);
int main(void){
    { int d=0,s=0; assert(strcmp(cplus_ecvt(1234.0,4,&d,&s),"1234")==0); assert(d==4 && s==0); }
    { int d=0,s=0; assert(strcmp(cplus_ecvt(0.0,1,&d,&s),"0")==0); assert(s==0); }
    { int d=0,s=0; assert(strcmp(cplus_fcvt(1234.5,2,&d,&s),"123450")==0); assert(d==4 && s==0); }
    { char buf[64]; assert(strcmp(cplus_gcvt(123.456,6,buf),"123.456")==0); }
    for(int i=0;i<10;i++){ double x=cplus_drand48(); assert(x>=0.0 && x<1.0); }
    for(int i=0;i<10;i++){ long l=cplus_lrand48(); assert(l>=0); }
    srand48(42); double a=cplus_drand48(); srand48(42); double b=cplus_drand48();
    assert(a==b);                                    /* deterministic given the same seed */
    { unsigned short seed[3]={123,456,789}; double e=cplus_erand48(seed); assert(e>=0.0 && e<1.0); }
    { unsigned short seed[3]={1,2,3}; unsigned short *p=cplus_seed48(seed); assert(p!=NULL); }
    return 0;
}
