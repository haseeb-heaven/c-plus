#include <stdio.h>
#define OFF 9
int main(void){ int a[4]={1,2,3,4}; int *p=a+OFF; printf("%d\n", p[0]); return 0; }
