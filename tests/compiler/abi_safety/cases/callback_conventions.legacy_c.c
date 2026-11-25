#include <stdio.h>
#include <stdlib.h>

struct Pair { long x; double y; };
static int cb_add(int a,int b){ int t=a+b; return t; }
static double cb_mul(double a,double b){ double t=a*b; return t; }
static struct Pair cb_pair(long x,double y){ struct Pair p; p.x=x; p.y=y; return p; }
static int cmp_desc(const void *a,const void *b){
  int x=*(const int*)a; int y=*(const int*)b;
  if(x>y)return -1; if(x<y)return 1; return 0;
}
static int invoke2(int (*f)(int,int),int a,int b){ int t=f(a,b); return t; }
static double invoke_dbl(double (*f)(double,double),double a,double b){
  double t=f(a,b); return t;
}
static struct Pair invoke_pair(struct Pair (*f)(long,double),long a,double b){
  struct Pair p=f(a,b); return p;
}
int main(void){
  struct Pair p=invoke_pair(cb_pair,7,2.5);
  int arr[6]; arr[0]=3;arr[1]=1;arr[2]=5;arr[3]=2;arr[4]=6;arr[5]=4;
  qsort(arr,6,sizeof(int),cmp_desc);
  printf("call2=%d calld=%.1f pair=%ld,%.1f qsort=%d%d%d%d%d%d\n",
         invoke2(cb_add,20,22), invoke_dbl(cb_mul,1.5,4.0), p.x, p.y,
         arr[0],arr[1],arr[2],arr[3],arr[4],arr[5]);
  return 0;
}
