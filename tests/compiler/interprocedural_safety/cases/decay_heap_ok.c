#include <stdlib.h>
#include <stdio.h>

static int first(int *p){ return p[3]; }
int main(void){ int *a=malloc(16); if(!a)return 2; a[3]=6;
  printf("v=%d\n",first(a)); free(a); return 0; }
