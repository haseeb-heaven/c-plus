
#include <stdlib.h>
int main(void){ int *p=malloc(4); if(!p) return 1; void(*f)(void*)=free; f(p); free(p); return 0; }
