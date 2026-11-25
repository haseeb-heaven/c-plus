
#include <stdlib.h>
#include <stdio.h>
int main(void){ int *p=malloc(4); if(!p)return 1; *p=71;
 void*(*r)(void*,size_t)=realloc; int *q=(int*)r(p,16); if(!q)return 1;
 printf("%d\n",*p); free(q); return 0; }
