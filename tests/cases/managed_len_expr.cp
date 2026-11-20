#include "cplus/cplus.h"
int main(void) {
  managed int *xs = cplus_new_array(int, 7);
  if (xs.len != 7) return 1;
  for (auto i = 0; i < xs.len; i++) xs[i] = i + 1;
  return xs[6] == 7 ? 0 : 2;
}
