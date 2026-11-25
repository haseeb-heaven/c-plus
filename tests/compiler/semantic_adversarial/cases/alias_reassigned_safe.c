#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; int b[16]={0}; int *p=a; p=b; p[9]=7; printf("%d\n", p[9]); return 0; }
