#include <stdlib.h>
#include <stdio.h>

static int *id(int *p){ return p; }
int main(void){ int *a=malloc(16); if(!a)return 2; int *q=id(a);
  q[3]=5; printf("v=%d\n",q[3]); free(a); return 0; }
