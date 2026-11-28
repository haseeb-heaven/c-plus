
#include <stdlib.h>
int main(void){ int *p=malloc(sizeof(int)); int *q=(int*)(void*)p; free(p); return *q; }
