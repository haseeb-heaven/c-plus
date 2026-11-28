#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; int *p=a; int *q=p; printf("%d\n", q[9]); return 0; }
