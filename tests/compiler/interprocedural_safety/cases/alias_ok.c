#include <stdio.h>

static int rd(int *p){ int *q=p; return q[2]; }
int main(void){ int a[4]={1,2,3,4}; printf("v=%d\n",rd(a)); return 0; }
