#include <stdlib.h>

static void put(int *p){ p[10]=1; }
int main(void){ int *a=malloc(8); if(!a)return 2; put(a); free(a); return 0; }
