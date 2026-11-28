#include <stdio.h>

static void fill(int *p,int n){ for(int i=0;i<n;i++) p[i]=i; }
int main(void){ int a[4]={0}; fill(a,4); printf("v=%d\n",a[3]); return 0; }
