#include <stdio.h>

int g[4]={1,2,3,4};
static int rd(void){ return g[2]; }
int main(void){ printf("v=%d\n",rd()); return 0; }
