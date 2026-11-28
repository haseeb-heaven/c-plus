#include <stdio.h>

static int rd(int *p){ return p[1]; }
int main(void){ int a[4]={1,2,3,4}; printf("v=%d\n",rd(a)); return 0; }
