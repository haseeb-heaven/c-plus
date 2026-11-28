#include <stdio.h>
static int f(int x,int y){ return x+y; }
int main(void){ int a[4]={1,2,3,4}; printf("%d\n", f(a[1],a[9])); return 0; }
