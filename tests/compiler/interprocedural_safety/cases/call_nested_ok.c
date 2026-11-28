#include <stdio.h>

static void g(int *p){ p[2]=8; }
static void f(int *p){ g(p); }
int main(void){ int a[4]={0}; f(a); printf("v=%d\n",a[2]); return 0; }
