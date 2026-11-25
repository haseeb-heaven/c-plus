#include <stdio.h>

static void h(int *p){ p[3]=6; }
static void g(int *p){ h(p); }
static void f(int *p){ g(p); }
int main(void){ int a[4]={0}; f(a); printf("v=%d\n",a[3]); return 0; }
