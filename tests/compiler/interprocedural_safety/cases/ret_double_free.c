#include <stdlib.h>

static void rel(int *p){ free(p); }
int main(void){ int *a=malloc(4); if(!a)return 2; rel(a); rel(a); return 0; }
