#include <stddef.h>
int main(void) {
  int a[4] = {0, 0, 0, 0};
  volatile size_t idx = 4;
  int *p = a;
  p[idx] = 1;
  return a[0];
}
