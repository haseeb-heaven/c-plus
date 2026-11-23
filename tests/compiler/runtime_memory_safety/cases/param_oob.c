#include <stdio.h>

static volatile int idx = 5;
static int rd(int *p){ return p[idx]; }
int main(void){ int a[4]={1,2,3,4}; printf("v=%d\n", rd(a)); return 0; }
