#include <stdlib.h>
int main(void){ int *p=malloc(8*sizeof(int)); if(!p)return 1; char *q=(char*)p; q[64]=1; return q[64]; }
