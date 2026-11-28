// Auto-runtime probe: the same runtime division-by-zero inside a try frame.
// With --auto-runtime the diagnostic is still printed, the catch handler runs
// and execution continues after the try with the exact stdout asserted below.
#include "cplus/cplus.h"

static volatile int divisor = 0;

static int divide(int a, int b) { return a / b; }

int main(void) {
  print("start");
  try {
    int result = divide(10, divisor);
    print("in try", result);
  } catch (Exception ex) {
    print("caught", ex.code);
  }
  print("after try");
  return 0;
}
