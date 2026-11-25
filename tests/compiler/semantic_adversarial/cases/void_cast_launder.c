#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; void *v=a; int *p=(int*)v + 9; *p=7; printf("%d\n", *p); return 0; }
