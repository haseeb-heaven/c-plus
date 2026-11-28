#include "cplus/cplus.h"
#include <stdio.h>
@tagged union U { int i; float f; };
int main(void) {
  @tagged union U v;
  v.i = 7;
  printf("%f\n", v.f);
  return 0;
}
