/* Cake qualifiers erase to a bare T*: layout and calling convention are plain C. */
#include <stddef.h>
#include <stdio.h>

struct Pair { int a; int b; };

int main(void) {
  struct Pair pair = {1, 2};
  struct Pair *_Opt p = &pair;
  printf("PTR=%zu RAW=%zu OFF=%zu\n", sizeof(p), sizeof(void *),
         offsetof(struct Pair, b));
  return p != 0 ? 0 : 1;
}
