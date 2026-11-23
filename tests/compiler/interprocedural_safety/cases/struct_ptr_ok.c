#include <stdio.h>

struct B{ int *p; };
static int rd(struct B *b){ return b->p[2]; }
int main(void){ int a[4]={1,2,3,4}; struct B b; b.p=a;
  printf("v=%d\n",rd(&b)); return 0; }
