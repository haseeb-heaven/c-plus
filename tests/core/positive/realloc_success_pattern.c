#include <stdlib.h>
int main(void){ int *p=malloc(sizeof(int)); void *tmp=realloc(p,2*sizeof(int)); if(tmp!=NULL) p=tmp; if(!p) return 1; *p=7; free(p); return 0; }
