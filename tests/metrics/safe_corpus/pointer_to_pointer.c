#include <stdlib.h>
static void alloc_int(int **out){ *out=malloc(sizeof **out); }
int main(void){ int *p=0; alloc_int(&p); if(!p) return 2; *p=11; int v=*p; free(p); return v==11?0:1; }
