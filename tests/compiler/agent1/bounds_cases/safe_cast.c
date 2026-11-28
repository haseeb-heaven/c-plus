#include <stdlib.h>
#include <stdio.h>
int main(void){ int *p=malloc(2*sizeof(int)); if(!p)return 1; char *q=(char*)p; q[7]=3; printf("%d\n",q[7]); free(p); return 0; }
