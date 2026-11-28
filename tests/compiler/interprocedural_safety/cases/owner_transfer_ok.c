#include <stdlib.h>
#include <stdio.h>

static void rel(int *p){ free(p); }
int main(void){ int *a=malloc(4); if(!a)return 2; *a=9; rel(a);
  printf("done\n"); return 0; }
