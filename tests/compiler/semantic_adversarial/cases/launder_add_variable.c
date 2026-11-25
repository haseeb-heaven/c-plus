#include <stdio.h>
#include <stdint.h>
int main(void){ int a[4]={1,2,3,4}; uintptr_t u=(uintptr_t)a; int k=9; u += (uintptr_t)k*sizeof(int); int *p=(int*)u; p[0]=7; printf("%d\n", p[0]); return 0; }
