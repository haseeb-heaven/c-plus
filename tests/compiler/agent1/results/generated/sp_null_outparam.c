#include <stddef.h>
extern void *cplus_register_allocation(void *, size_t, const char *, int);
extern void cplus_free_registered(void *, const char *, int);
extern void *cplus_realloc_registered(void *, void *, size_t, const char *, int);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/null_outparam.c"
#include <stdio.h>
#include <stdlib.h>
static void set_ptr(int **q) { *q = cplus_register_allocation((malloc(sizeof(int))), 4, __FILE__, __LINE__); }
int main(void) {
  int *p = NULL;
  set_ptr(&p);
  if (!p) return 1;
  *p = 7;
  printf("%d\n", *p);
  cplus_free_registered(p, __FILE__, __LINE__);
  return 0;
}
