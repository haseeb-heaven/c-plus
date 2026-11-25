#include <stdlib.h>

static void wr(int *p){ p[6]=9; }
int main(void){ int *a=malloc(4*sizeof(int)); if(!a)return 2; wr(a); free(a); return 0; }
