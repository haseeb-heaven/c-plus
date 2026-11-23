#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/optimizer/cases/ptr_form_stack.c"
#include <stdio.h>
int main(int argc, char **argv){
  int a[6] = {10,20,30,40,50,60};
  int off;
  int *p, *q;
  (void)argv;
  off = argc + 1;
  p = a + off;
  q = p - 1;
  printf("ps=%d,%d\n", *p, *q);
  return 0;
}