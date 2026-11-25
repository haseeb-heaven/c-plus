#include <stdio.h>

static int deref(int *p){ if(!p) return -1; return *p; }
int main(void){ int a=6; printf("v=%d\n",deref(&a)); return 0; }
