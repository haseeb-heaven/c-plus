#include <stdio.h>
static int a[4]={1,2,3,4};
static void f(int *a){ printf("%d\n", a[9]); }
int main(void){ int b[64]={0}; f(b); return 0; }
