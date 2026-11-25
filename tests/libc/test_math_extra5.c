/* Covers the platform-backed math wrappers added in batch 3 (math3.c). */
#define _DEFAULT_SOURCE
#include <assert.h>
#include <math.h>
double cplus_nan(const char*); double cplus_nextafter(double,double); double cplus_fdim(double,double);
double cplus_remainder(double,double); double cplus_log1p(double); double cplus_expm1(double);
double cplus_asinh(double); double cplus_acosh(double); double cplus_atanh(double);
double cplus_erf(double); double cplus_erfc(double); double cplus_tgamma(double); double cplus_lgamma(double);
static int isclose(double a,double b){ double d=a-b; if(d<0)d=-d; return d<1e-9; }
int main(void){
    for(int i=1;i<=10;i++){
        double x=(double)i/5.0;
        assert(isnan(cplus_nan("")));
        assert(cplus_nan("")!=cplus_nan(""));            /* NaN compares unequal to itself */
        assert(cplus_nextafter(1.0,2.0)>1.0);
        assert(cplus_nextafter(1.0,0.0)<1.0);
        assert(isclose(cplus_fdim(3.0,1.0),2.0));
        assert(cplus_fdim(1.0,3.0)==0.0);
        assert(isclose(cplus_remainder(5.0,3.0),-1.0));
        assert(cplus_remainder(4.0,2.0)==0.0);
        assert(cplus_log1p(0.0)==0.0);
        assert(cplus_expm1(0.0)==0.0);
        assert(isclose(cplus_expm1(1.0),1.718281828459045));
        assert(isclose(cplus_asinh(0.0),0.0));
        assert(isclose(cplus_acosh(1.0),0.0));
        assert(isclose(cplus_atanh(0.0),0.0));
        assert(isclose(cplus_erf(0.0),0.0));
        assert(isclose(cplus_erfc(0.0),1.0));
        assert(isclose(cplus_tgamma(1.0),1.0));
        assert(isclose(cplus_tgamma(5.0),24.0));
        assert(isclose(cplus_lgamma(2.0),0.0));
        assert(cplus_erf(x)>=0.0 && cplus_erfc(x)<=1.0);
    }
    return 0;
}
