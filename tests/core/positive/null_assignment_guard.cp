#include <stdio.h>
#include "cplus/cplus.h"
#include <stdlib.h>
int main(void) {
  int *p = malloc(4);
  p = (int*)NULL;
  if (p) printf("%d\n", *p);
  return p ? *p : 0;
}
