#include <stdio.h>
#include <stdlib.h>

static volatile int idx = 5;
int main(void){ int *a=malloc(16); if(!a)return 2; for(int i=0;i<4;i++)a[i]=i;
  printf("v=%d\n", a[idx]); free(a); return 0; }
