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
