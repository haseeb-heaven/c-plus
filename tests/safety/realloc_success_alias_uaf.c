#include <stdlib.h>
int main(void){ int *p=malloc(sizeof(int)); int *q=p; void *tmp=realloc(p,2*sizeof(int)); if(tmp!=NULL){p=tmp;return *q;} free(p); return 0; }
