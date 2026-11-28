#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; int *p=a; int **pp=&p; printf("%d\n", (*pp)[9]); return 0; }
