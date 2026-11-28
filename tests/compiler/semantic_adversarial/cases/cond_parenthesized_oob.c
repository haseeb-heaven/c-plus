#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; int f=0; printf("%d\n", a[(f?3:20)]); return 0; }
