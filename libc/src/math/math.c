#include "../../log/log.h"
#include <math.h>

double cplus_fabs(double x){ cplus_libc_log("fabs","enter"); return fabs(x); }
double cplus_sqrt(double x){ cplus_libc_log("sqrt","enter"); return sqrt(x); }
double cplus_sin(double x){ cplus_libc_log("sin","enter"); return sin(x); }
double cplus_cos(double x){ cplus_libc_log("cos","enter"); return cos(x); }
double cplus_tan(double x){ cplus_libc_log("tan","enter"); return tan(x); }
double cplus_pow(double x,double y){ cplus_libc_log("pow","enter"); return pow(x,y); }
double cplus_exp(double x){ cplus_libc_log("exp","enter"); return exp(x); }
double cplus_log(double x){ cplus_libc_log("log","enter"); return log(x); }
double cplus_log10(double x){ cplus_libc_log("log10","enter"); return log10(x); }
double cplus_floor(double x){ cplus_libc_log("floor","enter"); return floor(x); }
double cplus_ceil(double x){ cplus_libc_log("ceil","enter"); return ceil(x); }
double cplus_fmod(double x,double y){ cplus_libc_log("fmod","enter"); return fmod(x,y); }
