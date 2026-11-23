#include <stdio.h>
#include <stdlib.h>

int main(void){ int *p=malloc(16); if(!p)return 2; for(int i=0;i<4;i++)p[i]=i;
  int *q=realloc(p,64); if(!q){free(p);return 2;} printf("v=%d\n", p[0]); free(q); return 0; }
