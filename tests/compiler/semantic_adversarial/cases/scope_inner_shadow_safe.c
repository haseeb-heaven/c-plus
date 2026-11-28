#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; { int a[64]={0}; a[9]=7; printf("%d\n", a[9]); } printf("%d\n", a[0]); return 0; }
