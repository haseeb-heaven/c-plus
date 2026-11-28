#include <stdio.h>

static volatile int idx = 1;
int main(void){ int a[8]={0,1,2,3,4,5,6,7}; int *p=&a[2]; printf("v=%d\n", p[idx]); return 0; }
