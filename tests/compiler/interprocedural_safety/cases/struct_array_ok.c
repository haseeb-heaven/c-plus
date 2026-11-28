#include <stdio.h>

struct B{ int a[4]; };
static int rd(struct B *b){ return b->a[2]; }
int main(void){ struct B b={{1,2,3,4}}; printf("v=%d\n",rd(&b)); return 0; }
