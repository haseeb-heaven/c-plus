#include <assert.h>
#include <math.h>
#include <time.h>
#include <locale.h>
#include <string.h>
#include <stdio.h>

double cplus_fabs(double); double cplus_sqrt(double); double cplus_sin(double); double cplus_cos(double); double cplus_tan(double);
double cplus_pow(double,double); double cplus_exp(double); double cplus_log(double); double cplus_log10(double);
double cplus_floor(double); double cplus_ceil(double); double cplus_fmod(double,double);
time_t cplus_time(time_t*); double cplus_difftime(time_t,time_t); clock_t cplus_clock(void);
struct tm *cplus_gmtime(const time_t*); struct tm *cplus_localtime(const time_t*); time_t cplus_mktime(struct tm*);
size_t cplus_strftime(char*,size_t,const char*,const struct tm*);
char *cplus_setlocale(int,const char*); struct lconv *cplus_localeconv(void);

static int near(double a,double b){ double d=a-b; if(d<0)d=-d; return d < 1e-9; }

int main(void){
    for(int i=1;i<=10;i++){
        double x=(double)i/10.0;
        assert(near(cplus_fabs(-x),x));
        assert(near(cplus_sqrt(x*x),x));
        assert(near(cplus_sin(0.0),0.0));
        assert(near(cplus_cos(0.0),1.0));
        assert(cplus_floor(x+0.1)<=x+0.1);
        assert(cplus_ceil(x-0.1)>=x-0.1);
        assert(near(cplus_pow(2.0,3.0),8.0));
        assert(cplus_exp(0.0)>0.99 && cplus_exp(0.0)<1.01);
        assert(cplus_log(1.0)>-0.01 && cplus_log(1.0)<0.01);
        assert(cplus_log10(100.0)>1.99 && cplus_log10(100.0)<2.01);
        assert(near(cplus_fmod(5.0,2.0),1.0));
        assert(near(cplus_tan(0.0),0.0));
    }
    time_t t=cplus_time(NULL);
    assert(cplus_difftime(t,t)==0.0);
    (void)cplus_clock();
    struct tm *g=cplus_gmtime(&t); assert(g!=NULL);
    struct tm *l=cplus_localtime(&t); assert(l!=NULL);
    struct tm cp=*g; time_t m=cplus_mktime(&cp); (void)m;
    char buf[64]; assert(cplus_strftime(buf,sizeof(buf),"%Y",g)>0);

    char *loc=cplus_setlocale(LC_ALL,"C"); assert(loc!=NULL);
    struct lconv *lc=cplus_localeconv(); assert(lc!=NULL);
    return 0;
}
