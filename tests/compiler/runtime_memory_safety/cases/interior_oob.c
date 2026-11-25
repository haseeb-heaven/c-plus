#include <stdio.h>
#include <stdlib.h>

static volatile int idx = 5;
int main(void){ int *a=malloc(4*sizeof(int)); if(!a)return 2; for(int i=0;i<4;i++)a[i]=i;
  int *p=&a[2]; printf("v=%d\n", p[idx]); free(a); return 0; }
