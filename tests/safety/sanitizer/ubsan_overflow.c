#include <stdint.h>
int main(void) {
  int32_t x = 2147483647;
  volatile int32_t one = 1;
  x = x + one;
  return x == 0 ? 0 : 1;
}
