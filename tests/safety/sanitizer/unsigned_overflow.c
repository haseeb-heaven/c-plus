#include <stdint.h>
int main(void) {
  uint32_t value = 4294967295u;
  volatile uint32_t one = 1;
  value = value + one;
  return value == 0 ? 0 : 1;
}
