#include <stdio.h>

static int rec(int *p,int i){ if(i>=3) return p[i]; return rec(p,i+1); }
int main(void){ int a[4]={1,2,3,4}; printf("v=%d\n",rec(a,0)); return 0; }
