#include <stdlib.h>

static int *id(int *p){ return p; }
int main(void){ int *a=malloc(16); if(!a)return 2; int *q=id(a);
  q[6]=1; free(a); return 0; }
