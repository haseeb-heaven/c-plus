#include <stdio.h>
#include <stdlib.h>

static int *id(int *p){ return p; }
int main(void){ int *a=malloc(16); if(!a)return 2; for(int i=0;i<4;i++)a[i]=i;
  int *q=id(a); printf("v=%d\n", q[2]); free(a); return 0; }
