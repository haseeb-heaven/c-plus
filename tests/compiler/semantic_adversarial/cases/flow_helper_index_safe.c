#include <stdio.h>
static int idx(void){ return 2; }
int main(void){ int a[4]={1,2,3,4}; int i=idx(); printf("%d\n", a[i]); return 0; }
