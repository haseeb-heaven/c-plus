#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/realloc_success_pattern.c"
#include <stdlib.h>
int main(void){ int *p=malloc(sizeof(int)); void *tmp=realloc(p,2*sizeof(int)); if(tmp!=NULL) p=tmp; if(!p) return 1; *p=7; free(p); return 0; }