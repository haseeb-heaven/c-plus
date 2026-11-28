#include <stdio.h>
#include <stdint.h>
int main(void){ int a[4]={1,2,3,4}; uintptr_t u=((uintptr_t)a)|1; int *p=(int*)(u & ~(uintptr_t)1); p[9]=7; printf("%d\n", p[9]); return 0; }
