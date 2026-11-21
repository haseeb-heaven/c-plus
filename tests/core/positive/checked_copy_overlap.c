#include "cplus/cplus.h"
#include <string.h>
int main(void) {
  char b[16];
  for (int i = 0; i < 16; i++) b[i] = (char)('a' + i);
  cplus_memmove(b + 2, b, 10);
  return b[0] == 'a' && b[1] == 'b' && b[2] == 'a' ? 0 : 1;
}
