#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; char *b=(char*)a; b[36]=7; printf("%d\n", a[0]); return 0; }
