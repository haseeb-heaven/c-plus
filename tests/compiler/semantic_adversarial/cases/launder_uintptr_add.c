#include <stdio.h>
#include <stdint.h>
int main(void){ int a[4]={1,2,3,4}; uintptr_t u=(uintptr_t)a + 9*sizeof(int); int *p=(int*)u; *p=7; printf("%d\n", *p); return 0; }
