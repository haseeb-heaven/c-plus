#include <stdio.h>
#include <stdlib.h>

int main(void){ int *p=malloc(0); printf("z=%p\n",(void*)p); free(p); return 0; }
