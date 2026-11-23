#include <stdio.h>

static volatile int n = 2;
int main(void){ int a[4]={1,2,3,4}; int *p=a; p+=n; printf("v=%d\n", *p); return 0; }
