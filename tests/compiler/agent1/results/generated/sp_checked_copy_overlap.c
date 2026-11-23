#include <stddef.h>
extern size_t __cplus_bounds_check(size_t, size_t, const char *);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/checked_copy_overlap.c"
#include "cplus/cplus.h"
#include <string.h>
int main(void) {
  char b[16];
  for (int i = 0; i < 16; i++) b[__cplus_bounds_check((size_t)(i), 16, "b")] = (char)('a' + i);
  cplus_memmove(b + 2, b, 10);
  return b[0] == 'a' && b[1] == 'b' && b[2] == 'a' ? 0 : 1;
}