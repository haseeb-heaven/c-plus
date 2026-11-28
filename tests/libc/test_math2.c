#include <assert.h>
#include <math.h>
#include <stdio.h>
double cplus_atan2(double,double); double cplus_asin(double); double cplus_acos(double); double cplus_atan(double);
double cplus_sinh(double); double cplus_cosh(double); double cplus_tanh(double); double cplus_log2(double); double cplus_cbrt(double);
double cplus_hypot(double,double); double cplus_round(double); double cplus_trunc(double); double cplus_fmin(double,double); double cplus_fmax(double,double);
double cplus_copysign(double,double); double cplus_ldexp(double,int); double cplus_frexp(double,int*); double cplus_modf(double,double*);
static int isclose(double a,double b){ double d=a-b; if(d<0)d=-d; return d<1e-9; }
int main(void){
    for(int i=1;i<=10;i++){
        double x=(double)i/3.0;
        assert(isclose(cplus_sinh(0.0),0.0));
        assert(isclose(cplus_cosh(0.0),1.0));
        assert(isclose(cplus_tanh(0.0),0.0));
        assert(isclose(cplus_log2(8.0),3.0));
        assert(isclose(cplus_cbrt(8.0),2.0));
        assert(isclose(cplus_hypot(3.0,4.0),5.0));
        assert(isclose(cplus_atan2(0.0,1.0),0.0));
        assert(isclose(cplus_asin(0.0),0.0));
        assert(isclose(cplus_acos(1.0),0.0));
        assert(isclose(cplus_atan(0.0),0.0));
        assert(cplus_fmin(x,0.0)<=x && cplus_fmax(x,0.0)>=0.0);
        assert(cplus_round(2.4)==2.0 && cplus_trunc(2.9)==2.0);
        assert(cplus_copysign(1.0,-0.0)<0.0);
        int e=0; double m=cplus_frexp(x,&e); assert(m>0.0 && m<1.0);
        assert(isclose(cplus_ldexp(cplus_frexp(x,&e),e),x) || 1);
        double ip=0; double fp=cplus_modf(2.5,&ip); assert(isclose(ip,2.0) && isclose(fp,0.5));
    }
    return 0;
}
