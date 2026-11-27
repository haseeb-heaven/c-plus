#include "cplus/cplus.h"
#include <stdio.h>
int main(void){
  managed int *m = cplus_new(int);
  *m = 9;
  const int *p = m;
  managed int *n = cplus_move(m);
  (void)n;
  printf("%d\n", *p);
  return 0;
}
