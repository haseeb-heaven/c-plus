#include <stdio.h>
int main(void){ int a[4]={1,2,3,4}; long u=(long)(void*)a; int *p=(int*)(u + 9L*4L); p[0]=7; printf("%d\n", p[0]); return 0; }
