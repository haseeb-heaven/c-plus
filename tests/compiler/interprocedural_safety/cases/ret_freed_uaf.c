#include <stdlib.h>

static void rel(int *p){ free(p); }
int main(void){ int *a=malloc(16); if(!a)return 2; rel(a); int v=*a; (void)v; return 0; }
