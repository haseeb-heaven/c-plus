// Auto-runtime probe: an opaque divisor keeps the failure runtime-reachable.
// With auto-runtime off this must keep the original deterministic C+ exit
// (CPLUS_EXIT_ARITHMETIC = 110).  With it on, but no live try frame, the
// runtime must still exit with 110 rather than silently continuing.
#include "cplus/cplus.h"

static volatile int divisor = 0;

static int divide(int a, int b) { return a / b; }

int main(void) {
  print("start");
  int result = divide(10, divisor);
  print("unreachable", result);
  return 0;
}
