#include <stdio.h>
#include <string.h>

/* Real-world style: a tagged variant carrying an integer or a double. */
union Value { long i; double d; unsigned char bytes[16]; };
int main(void) {
  union Value v;
  size_t k;
  memset(&v, 0, sizeof v); v.d = 2.5;
  printf("size=%zu d=%.1f:", sizeof(union Value), v.d);
  for (k = 0; k < sizeof v.bytes; k++) printf("%02x", v.bytes[k]);
  printf("\n");
  memset(&v, 0, sizeof v); v.i = -7L;
  printf("i=%ld:", v.i);
  for (k = 0; k < sizeof v.bytes; k++) printf("%02x", v.bytes[k]);
  printf("\n");
  return 0;
}
