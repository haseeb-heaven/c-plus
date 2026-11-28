#include <stdio.h>

static volatile int idx = 2;
static int first(int *p){ return p[idx]; }
int main(void){ int a[4]={1,2,3,4}; printf("v=%d\n", first(a)); return 0; }
