#include <stdio.h>
int main(int argc, char **argv){
  int d, q, r;
  (void)argv;
  d = argc + 6;
  q = 1000 / d;
  r = 1000 % d;
  printf("div=%d,%d\n", q, r);
  return 0;
}
