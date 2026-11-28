#include <stdlib.h>

int main(void){ int n=2147483647; int *p=malloc(n*4); if(!p)return 2;
  p[0]=1; free(p); return 0; }
