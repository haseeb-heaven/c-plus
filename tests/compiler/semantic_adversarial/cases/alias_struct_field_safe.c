#include <stdio.h>
struct S { int *p; };
int main(void){ int a[4]={1,2,3,4}; struct S s; s.p=a; printf("%d\n", s.p[2]); return 0; }
