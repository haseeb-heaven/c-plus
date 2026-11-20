#include "cplus/cplus.h"
int main(void) {
  int v[] = {1,2,3,4,5};
  auto sum = 0;
  for (auto x : v) sum += x;
  if (v.len != 5 || countof(v) != 5) return 1;
  return sum == 15 ? 0 : 2;
}
