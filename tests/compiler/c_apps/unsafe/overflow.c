/* Unsafe: runtime signed overflow from a non-constant source. STRICT build
 * must trap natively with --sanitize=none (rc 110), no sanitizer needed. */
#include <stdlib.h>
static int add1(int x) { return x + 1; }
int main(void) {
  int x = atoi("2147483647");
  return add1(x);
}
