#include <stdio.h>

static int g(int*,int);
static int f(int *p,int i){ if(i>=3) return p[i]; return g(p,i+1); }
static int g(int *p,int i){ return f(p,i); }
int main(void){ int a[4]={1,2,3,4}; printf("v=%d\n",f(a,0)); return 0; }
