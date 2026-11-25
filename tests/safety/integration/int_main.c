#include <stdio.h>
#include <stdlib.h>
#include "cplus/cplus.h"
#include "int_header.h"
int main(void) {
  const size_t count = 8;
  unsigned char *buffer = cplus_alloc(count);
  if (!buffer) return 2;
  int_fill(buffer, count);
  int total = int_sum(buffer, count);
  cplus_release(buffer);
  if (total != 36) return 3;
  printf("integration ok %d\n", total);
  return 0;
}
