#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; volatile int v=2; int i=v; printf("%d\n", a[i]); return 0; }
