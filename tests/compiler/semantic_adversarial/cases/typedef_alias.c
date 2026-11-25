#include <stdio.h>
typedef int *IP;
int main(void){ int a[4]={1,2,3,4}; IP p=a; printf("%d\n", p[9]); return 0; }
