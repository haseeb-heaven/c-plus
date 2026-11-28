/* Covers the platform-backed math wrappers added in batch 4 (math4.c). */
#define _DEFAULT_SOURCE
#include <assert.h>
#include <math.h>

double cplus_fma(double,double,double); double cplus_remquo(double,double,int*);
double cplus_logb(double); int cplus_ilogb(double); float cplus_nanf(const char*);
double cplus_exp2(double); double cplus_nearbyint(double); long cplus_lrint(double);
long long cplus_llrint(double); double cplus_scalbn(double,int);

static int isclose(double a,double b){ double d=a-b; if(d<0)d=-d; return d<1e-9; }

int main(void){
    /* fma: exact fused multiply-add */
    assert(cplus_fma(2.0,3.0,1.0)==7.0);
    assert(isclose(cplus_fma(0.5,4.0,0.0),2.0));

    /* scalbn: x * 2^n */
    assert(isclose(cplus_scalbn(1.0,3),8.0));
    assert(isclose(cplus_scalbn(1.5,2),6.0));
    assert(isclose(cplus_scalbn(8.0,-3),1.0));

    /* exp2 */
    assert(isclose(cplus_exp2(3.0),8.0));
    assert(isclose(cplus_exp2(0.0),1.0));

    /* logb / ilogb: unbiased exponent */
    assert(isclose(cplus_logb(8.0),3.0));
    assert(isclose(cplus_logb(0.5),-1.0));
    assert(cplus_ilogb(8.0)==3);
    assert(cplus_ilogb(0.5)==-1);

    /* nanf produces a quiet NaN */
    assert(isnan(cplus_nanf("")));
    assert(cplus_nanf("")!=cplus_nanf(""));       /* NaN != NaN */

    /* nearbyint / lrint / llrint: round to nearest under the default mode */
    assert(cplus_nearbyint(2.4)==2.0);
    assert(cplus_nearbyint(2.6)==3.0);
    assert(cplus_lrint(2.4)==2);
    assert(cplus_lrint(2.6)==3);
    assert(cplus_llrint(2.4)==2);
    assert(cplus_llrint(-2.6)==-3);

    /* remquo: remainder plus quotient (low bits) */
    { int q=0; double r=cplus_remquo(10.0,3.0,&q); assert(isclose(r,1.0)); assert((q&7)==3); }
    { int q=0; double r=cplus_remquo(7.0,2.0,&q); assert(isclose(r,-1.0)); }

    for(int i=1;i<=10;i++) assert(cplus_exp2((double)i)==(double)(1<<i));
    return 0;
}
