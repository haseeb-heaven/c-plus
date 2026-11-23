#include <stdio.h>
#include <stdlib.h>

static volatile int idx = 5;
static int rd(int *p){ return p[idx]; }
int main(void){ int *a=malloc(16); if(!a)return 2; for(int i=0;i<4;i++)a[i]=i;
  printf("v=%d\n", rd(a)); free(a); return 0; }
