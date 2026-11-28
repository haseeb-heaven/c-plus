#include <stdio.h>

static volatile int idx = 6;
int main(void){ int a[4]={1,2,3,4}; void *v=a; int *p=(int*)v; printf("v=%d\n", p[idx]); return 0; }
