#include <stdio.h>
#include <stdint.h>
#define LAUNDER(x) ((int*)(uintptr_t)(x) + 9)
int main(void){ int a[4]={1,2,3,4}; int *p=LAUNDER(a); printf("%d\n", p[0]); return 0; }
