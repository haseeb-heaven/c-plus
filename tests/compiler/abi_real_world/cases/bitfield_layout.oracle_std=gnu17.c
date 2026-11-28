#include <stdio.h>
#include <string.h>

/* Real-world style: packed device registers described with bit-fields
   (offsetof is not defined for bit-fields, so the raw bytes are inspected). */
struct Flags { unsigned a:3; unsigned b:5; unsigned c:8; unsigned d:16; };
struct Reg { unsigned a:3; unsigned b:5; unsigned :0; unsigned c:9; int d:7; };
int main(void) {
  struct Flags f;
  unsigned char raw[sizeof(struct Flags)];
  size_t i;
  memset(&f, 0, sizeof f); f.a = 5; f.b = 17; f.c = 0xA5; f.d = 0x1234;
  memcpy(raw, &f, sizeof raw);
  printf("flags=%zu:", sizeof(struct Flags));
  for (i = 0; i < sizeof raw; i++) printf("%02x", raw[i]);
  printf("\n");
  memset(&f, 0, sizeof f); f.a = 1; f.d = 7;
  memcpy(raw, &f, sizeof raw);
  printf("sparse=%zu:", sizeof(struct Flags));
  for (i = 0; i < sizeof raw; i++) printf("%02x", raw[i]);
  printf("\n");
  {
    struct Reg r; unsigned char rraw[sizeof(struct Reg)];
    memset(&r, 0, sizeof r); r.a = 5; r.b = 17; r.c = 0x101; r.d = -3;
    memcpy(rraw, &r, sizeof rraw);
    printf("reg=%zu:", sizeof(struct Reg));
    for (i = 0; i < sizeof rraw; i++) printf("%02x", rraw[i]);
    printf(",%u,%u,%u,%d\n", r.a, r.b, r.c, r.d);
  }
  return 0;
}
