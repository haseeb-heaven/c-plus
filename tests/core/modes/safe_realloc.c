#include <stdlib.h>
int main(void){ int *p=malloc(sizeof(int)); void *t=realloc(p,sizeof(int)*2); if(t) p=t; if(!p)return 1; *p=7; free(p); return 0; }
