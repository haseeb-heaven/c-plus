#include <stdint.h>
#include <stdlib.h>
int main(void){ int*p=(int*)malloc(sizeof(int)); int*tagged=(int*)((uintptr_t)p|1ULL); free(tagged); return 0; }
