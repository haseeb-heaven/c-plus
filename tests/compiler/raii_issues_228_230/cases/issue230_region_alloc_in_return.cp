#include <stdio.h>
static int store(int *p){ *p = 7; printf("%d\n", *p); return 0; }
int main(void){ region r { return store((int *)cplus_region_alloc(r, sizeof(int))); } }
