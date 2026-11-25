#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; char *b=(char*)a; printf("%d\n", (int)b[4]); return 0; }
