#include <stddef.h>
int main(void) {
  int a[4] = {0, 0, 0, 0};
  volatile size_t idx = 4;
  a[idx] = 1;
  return a[0];
}
