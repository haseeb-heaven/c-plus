#include <stdlib.h>

static int first(int *p){ return p[4]; }
int main(void){ int *a=malloc(16); if(!a)return 2; int v=first(a); (void)v; free(a); return 0; }
