#include <stdint.h>
#include <stdlib.h>
int main(void){ int*p=(int*)malloc(sizeof(int)); int*tagged=(int*)((uintptr_t)p|1ULL); *tagged=42; free((void*)((uintptr_t)tagged&~1ULL)); return 0; }
