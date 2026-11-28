#include <stdio.h>
#include <stdlib.h>

static volatile int idx = 4;
struct B{ int *p; };
static void wr(struct B *b){ b->p[idx]=1; }
int main(void){ int *a=malloc(4*sizeof(int)); if(!a)return 2; for(int i=0;i<4;i++)a[i]=i;
  struct B b; b.p=a; wr(&b); printf("v=%d\n", a[0]); free(a); return 0; }
