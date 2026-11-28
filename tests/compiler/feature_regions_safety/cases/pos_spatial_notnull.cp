#include "cplus/cplus.h"
#include <stdio.h>
static void show(int *@notnull p) { printf("%d\n", *p); }
int main(void){ int x=7; show(&x); return 0; }
