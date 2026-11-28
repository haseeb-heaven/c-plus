#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/optimizer/cases/arith_addmul.c"
#include <stdio.h>
int main(int argc, char **argv){
  int a, b, c;
  (void)argv;
  a = argc + 10;
  b = a * 3;
  c = b - a;
  printf("arith=%d,%d,%d\n", a, b, c);
  return 0;
}