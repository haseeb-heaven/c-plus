#if 0
  This comment shouldn't break RAII!
  and a doubled apostrophe'' is disabled too
#endif
// A spliced comment must not become C+ code: \
class Fake { int ignored; };
#include <stdio.h>
class Real { int value; int get(void){ return this->value; } };
int main(void) {
  Real r = {7};
  printf("%d\n", r.get());
  return 0;
}
