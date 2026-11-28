/* Runtime-dependent equivalent of scratch/demo.c: the count comes from a
 * volatile-backed function so the C+ static checker cannot prove the
 * off-by-one, and the overflow only appears when ASan is enabled. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct Packet { uint32_t count; uint32_t element_size; };

static uint32_t runtime_count(void) {
  volatile uint32_t n = 4;
  return n;
}

static void write_packets(unsigned char *buffer, const struct Packet *packet) {
  for (uint32_t i = 0; i <= packet->count; i++) {
    memset(buffer + ((size_t)i * packet->element_size),
           0x41, packet->element_size);
  }
}

int main(void) {
  struct Packet packet = { .count = runtime_count(), .element_size = 16 };
  size_t bytes = (size_t)packet.count * packet.element_size;
  unsigned char *buffer = malloc(bytes);
  if (!buffer) return 1;
  write_packets(buffer, &packet);
  free(buffer);
  return 0;
}
