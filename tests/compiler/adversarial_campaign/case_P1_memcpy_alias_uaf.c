
#include <stdlib.h>
#include <string.h>
int main(void){ int *p=malloc(sizeof(int)); int *q; memcpy(&q,&p,sizeof p); free(p); return *q; }
