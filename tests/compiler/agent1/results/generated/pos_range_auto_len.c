#include <stddef.h>
extern size_t __cplus_bounds_check(size_t, size_t, const char *);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/range_auto_len.cp"
#include "cplus/cplus.h"
int main(void) {
  int v[] = {1,2,3,4,5};
  __typeof__( 0) sum = 0;
  for (size_t __cplus_range_i0 = 0; __cplus_range_i0 < (5); __cplus_range_i0++) { __typeof__(v[__cplus_bounds_check((size_t)(__cplus_range_i0), 5, "v")]) x = v[__cplus_bounds_check((size_t)(__cplus_range_i0), 5, "v")]; sum += x;}
  if ((5) != 5 || countof(v) != 5) return 1;
  return sum == 15 ? 0 : 2;
}
