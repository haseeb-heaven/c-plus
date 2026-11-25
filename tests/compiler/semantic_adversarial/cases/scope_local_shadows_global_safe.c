#include <stdio.h>
static int a[4]={1,2,3,4};
static void f(void){ int a[8]={0}; a[7]=42; printf("%d\n", a[7]); }
int main(void){ f(); printf("%d\n", a[0]); return 0; }
