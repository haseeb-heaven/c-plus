#include <stdio.h>
#include <string.h>

/* Real-world style: small, full-range and flag enumerations persisted to a
   binary format. */
enum Small { S_ZERO = 0, S_SEVEN = 7 };
enum Wide { W_MAX = 2147483647, W_NEG = -1 };
enum Flag { F_ONE = 1, F_BIG = 2147483646 };
int main(void) {
  enum Small s = S_SEVEN;
  enum Wide w = W_MAX;
  unsigned char raw[sizeof(enum Small)];
  size_t i;
  memcpy(raw, &s, sizeof raw);
  printf("e=%zu,%zu,%zu v=%d,%d,%d raw=", sizeof(enum Small),
         sizeof(enum Wide), sizeof(enum Flag), (int)s, (int)w, (int)F_BIG);
  for (i = 0; i < sizeof raw; i++) printf("%02x", raw[i]);
  printf("\n");
  return 0;
}
