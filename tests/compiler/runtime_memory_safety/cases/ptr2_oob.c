#include <stdio.h>

static volatile int idx = 6;
static void wr(int **pp){ (*pp)[idx]=1; }
int main(void){ int a[4]={0}; int *p=a; wr(&p); printf("v=%d\n", a[0]); return 0; }
