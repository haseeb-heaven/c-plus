#include <stdio.h>

static int rd(int **pp){ return (*pp)[2]; }
int main(void){ int a[4]={1,2,3,4}; int *p=a; printf("v=%d\n",rd(&p)); return 0; }
