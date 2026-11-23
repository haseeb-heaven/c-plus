#include <stdio.h>

static int add_i(int a,int b){ int t=a+b; return t; }
static long add_l(long a,long b,long c){ long t=a+b; long u=t+c; return u; }
static double mul_d(double a,double b){ double t=a*b; return t; }
static int pass_ptr(const int *p){ return *p; }
static void set_ptr(int *p,int v){ *p=v; }
int main(void){
  int slot=5; set_ptr(&slot,5);
  printf("i=%d l=%ld d=%.1f p=%d set=%d\n",
         add_i(20,22), add_l(1,2,3), mul_d(1.5,4.0),
         pass_ptr(&slot), slot);
  return 0;
}
