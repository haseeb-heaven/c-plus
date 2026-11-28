#include <stdio.h>

static volatile int idx = 2;
static int rd(int **pp){ return (*pp)[idx]; }
int main(void){ int a[4]={1,2,3,4}; int *p=a; printf("v=%d\n", rd(&p)); return 0; }
