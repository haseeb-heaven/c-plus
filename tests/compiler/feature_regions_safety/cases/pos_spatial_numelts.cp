#include "cplus/cplus.h"
#include <stdio.h>
static void read_at(int *@numelts(n) p, size_t n, int i) { printf("%d\n", p[i]); }
int main(void){ int a[4]={1,2,3,4}; read_at(a,4,2); return 0; }
