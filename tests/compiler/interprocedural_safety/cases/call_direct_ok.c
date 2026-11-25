#include <stdlib.h>
#include <stdio.h>

static void wr(int *p){ p[3]=9; }
int main(void){ int *a=malloc(4*sizeof(int)); if(!a)return 2; wr(a);
  printf("v=%d\n",a[3]); free(a); return 0; }
