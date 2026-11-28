#include <stdio.h>

int first(int *p){ return p[3]; }
int main(void){ int a[4]={1,2,3,5}; printf("v=%d\n",first(a)); return 0; }
