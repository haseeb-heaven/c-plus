#include "cplus/cplus.h"
#include <stdio.h>
@tagged union U { int i; float f; };
int main(void) {
  @tagged union U v;
  volatile int c = 1;
  if (c) v.i = 7; else v.f = 2.5f;
  printf("%f\n", v.f);
  return 0;
}
