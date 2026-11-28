#include <cplus/cplus.h>
#include <stddef.h>
int main(void) {
  unsigned char *buffer = cplus_alloc(4);
  if (!buffer) return 2;
  volatile size_t idx = 4;
  buffer[idx] = 0x41;
  cplus_release(buffer);
  return 0;
}
