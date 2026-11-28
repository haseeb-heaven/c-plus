#include <stddef.h>
extern void *cplus_register_allocation(void *, size_t, const char *, int);
extern void cplus_free_registered(void *, const char *, int);
extern void *cplus_realloc_registered(void *, void *, size_t, const char *, int);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/optimizer/cases/alloc_zero_realloc.c"
#include <stdio.h>
#include <stdlib.h>
int main(void){
  int *p, *q;
  p = cplus_register_allocation((calloc(4, sizeof(int))), 16, __FILE__, __LINE__);
  if(!p) return 2;
  p[0]=5;
  q = cplus_realloc_registered((p), (realloc(p, 8*sizeof(int))), 32, __FILE__, __LINE__);
  if(!q) return 3;
  q[7]=9;
  printf("az=%d,%d\n", q[0], q[7]);
  cplus_free_registered(q, __FILE__, __LINE__);
  return 0;
}