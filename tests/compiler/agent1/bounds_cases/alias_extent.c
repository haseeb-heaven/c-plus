#include <stdlib.h>
int main(void){ int *p=malloc(8*sizeof(int)); if(!p)return 1; int *q=p; q[8]=1; return q[8]; }
