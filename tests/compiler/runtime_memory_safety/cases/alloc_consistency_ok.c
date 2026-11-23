#include <stdio.h>
#include <stdlib.h>

int main(void){ int *p=calloc(4,sizeof(int)); if(!p)return 2; p[3]=7;
  printf("v=%d\n", p[3]); free(p);
  int *q=malloc(16); if(!q)return 2; q[0]=9; printf("w=%d\n", q[0]); free(q);
  printf("ok\n"); return 0; }
