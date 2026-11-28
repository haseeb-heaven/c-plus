
#include <stdlib.h>
#include <stdint.h>
int main(void){ int *p=malloc(sizeof(int)); uintptr_t u=(uintptr_t)p; int *q=(int*)u; free(p); return *q; }
