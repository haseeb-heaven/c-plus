#include <stdlib.h>

static void wr(int *p){ p[2]=1; }
int main(int argc,char **argv){ (void)argv; int a[4]={0}; int *q = argc ? a : 0;
  wr(q); return a[0]; }
