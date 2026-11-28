#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; { int a=7; (void)a; } printf("%d\n", a[3]); return 0; }
