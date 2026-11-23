#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/optimizer/cases/alloc_zero_realloc.c"
#include <stdio.h>
#include <stdlib.h>
int main(void){
  int *p, *q;
  p = calloc(4, sizeof(int));
  if(!p) return 2;
  p[0]=5;
  q = realloc(p, 8*sizeof(int));
  if(!q) return 3;
  q[7]=9;
  printf("az=%d,%d\n", q[0], q[7]);
  free(q);
  return 0;
}