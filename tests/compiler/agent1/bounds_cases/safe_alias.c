#include <stdlib.h>
#include <stdio.h>
int main(void){ int *p=malloc(2*sizeof(int)); if(!p)return 1; int *q=p; q[1]=7; printf("%d\n",q[1]); free(p); return 0; }
