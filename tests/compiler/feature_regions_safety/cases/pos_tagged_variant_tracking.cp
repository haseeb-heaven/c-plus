#include "cplus/cplus.h"
#include <stdio.h>
@tagged union U { int i; float f; };
int main(void) {
  @tagged union U v;
  v.i = 7;
  printf("%d\n", v.i);
  v.f = 2.5f;
  printf("%.1f\n", v.f);
  return 0;
}
