#include <stdio.h>

static volatile int idx = 8;
int g[4]={1,2,3,4};
int main(void){ printf("v=%d\n", g[idx]); return 0; }
