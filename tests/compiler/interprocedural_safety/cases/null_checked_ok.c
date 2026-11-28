#include <stdio.h>

static int deref(int *p){ if(!p) return -1; return *p; }
int main(void){ printf("v=%d\n",deref(0)); return 0; }
