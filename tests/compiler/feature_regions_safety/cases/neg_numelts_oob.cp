#include "cplus/cplus.h"
#include <stdio.h>
static void f(int *@numelts(4) p) { int v = p[8]; (void)v; }
int main(void) { int a[4] = {0,0,0,0}; f(a); return 0; }
