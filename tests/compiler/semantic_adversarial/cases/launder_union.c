#include <stdio.h>
#include <stdint.h>
union U { uintptr_t u; int *p; };
int main(void){ int a[4]={1,2,3,4}; union U x; x.u=(uintptr_t)a + 9*sizeof(int); printf("%d\n", x.p[0]); return 0; }
