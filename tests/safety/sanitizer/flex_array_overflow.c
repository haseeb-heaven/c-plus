#include <stdlib.h>
struct Packet { int count; unsigned char data[]; };
int main(void) {
  struct Packet *p = malloc(sizeof(*p) + 4);
  if (!p) return 2;
  p->count = 4;
  volatile int idx = 8;
  p->data[idx] = 0x41;
  return 0;
}
