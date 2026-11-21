#include <stdint.h>
#include <stdlib.h>
int main(void){ int*p=(int*)malloc(sizeof(int)); int*q=(int*)malloc(sizeof(int)); *q=10; uintptr_t ap=(uintptr_t)p; uintptr_t aq=(uintptr_t)q; int*forged=(int*)(ap+(aq-ap)); *forged=20; free(p);free(q); return 0; }
