#include <stdio.h>

static void wr(int *p){ for(int i=0;i<4;i++) p[i]=i; }
int main(void){ int a[4]={0}; wr(a); printf("v=%d\n",a[3]); return 0; }
