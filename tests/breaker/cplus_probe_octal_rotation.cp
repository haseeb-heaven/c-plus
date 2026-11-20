#include "cplus/cplus.h"
#include <stdint.h>
int main(void) {
  uint32_t x = 0x12345678u;
  return 0o755 == 493 && cplus_rotr32(cplus_rotl32(x, 7),7)==x ? 0 : 1;
}
