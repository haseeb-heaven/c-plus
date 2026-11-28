#include <stdio.h>

static volatile int idx = 2;
int main(void){ int a[4]={1,2,3,4}; printf("v=%d\n", a[idx]); return 0; }
