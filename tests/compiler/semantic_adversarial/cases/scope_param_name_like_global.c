#include <stdio.h>
static int a[4]={1,2,3,4};
static void f(int *a){ printf("%d\n", a[3]); }
int main(void){ int b[8]={0}; b[3]=42; f(b); return 0; }
