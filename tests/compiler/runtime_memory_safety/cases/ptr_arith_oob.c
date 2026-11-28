#include <stdio.h>

static volatile int n = 10;
int main(void){ int a[4]={1,2,3,4}; int *p=a; printf("v=%d\n", *(p+n)); return 0; }
