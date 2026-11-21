#include <stdlib.h>
int main(void){ int *p=malloc(sizeof(int)); void *t=realloc(p,sizeof(int)*2); (void)t; free(p); return 0; }  // NOSONAR intentional safe-mode safety fixture
