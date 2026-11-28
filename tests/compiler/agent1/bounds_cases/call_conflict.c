#include <stdlib.h>
static int f(int *p){ return p[7]; }
int main(void){ int *s=malloc(4*sizeof(int)); int *b=malloc(16*sizeof(int)); if(!s||!b)return 1; f(s); return f(b); }
