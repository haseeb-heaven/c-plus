#include "cplus/cplus.h"
int main(void) {
  managed int *p = cplus_new_array(int, 5);
  if (p.len != 5) return 1;
  for (size_t i = 0; i < p.len; i++) p[i] = (int)i;
  return p[4] == 4 ? 0 : 1;
}
