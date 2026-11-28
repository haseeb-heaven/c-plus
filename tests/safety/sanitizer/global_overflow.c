#include <stddef.h>
static int global_array[4] = {0, 0, 0, 0};
int main(void) {
  volatile size_t idx = 4;
  int *p = global_array;
  p[idx] = 7;
  return global_array[0];
}
