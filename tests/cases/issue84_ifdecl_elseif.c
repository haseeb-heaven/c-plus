/* Issue #84: an if-declaration followed by an else-if chain must keep its
 * scope-closing brace after the whole chain, not in the middle of it. */
#include "cplus/cplus.h"

static int get(void) { return 0; }

int main(void) {
  if (int x = get(); x > 0) {
    print("branch", "positive", x);
  } else if (x == 0) {
    print("branch", "zero", x);
  } else {
    print("branch", "negative", x);
  }

  if (int y = 2; y > 5)
    print("nope");
  else if (int z = 3; z > 2)
    print("nested", z);
  else
    print("nope2");

  if (int a = 0; a > 0) {
    print("A", a);
  } else if (int b = 1; b == 1) {
    print("B", b);
  } else if (int c = 2; c == 2) {
    print("C", c);
  } else {
    print("D");
  }
  return 0;
}
