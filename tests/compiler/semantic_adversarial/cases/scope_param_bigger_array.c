#include <stdio.h>
static void f(int *p){ printf("%d\n", p[9]); }
int main(void){ int big[16]={0}; big[9]=42; f(big); return 0; }
