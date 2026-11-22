/* Bug 765 (catalog scratch/bugs50.cp): reading a float through a uint32_t
 * lvalue violates strict aliasing; the bits must be copied with memcpy. */
#include <stdint.h>
#include <stdio.h>

int main(void) {
  float f = 3.14f;
  uint32_t bits = *(uint32_t *)&f; /* BUG 765: type-punned read */
  printf("0x%x\n", bits);
  return 0;
}
