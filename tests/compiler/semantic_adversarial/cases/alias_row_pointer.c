#include <stdio.h>
int main(void){ int a[2][4]={{1,2,3,4},{5,6,7,8}}; int (*r)[4]=a; printf("%d\n", r[9][0]); return 0; }
