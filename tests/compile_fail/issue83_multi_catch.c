#include "cplus/cplus.h"

/* Issue #83: a second catch clause used to be silently discarded. It must
 * now be diagnosed instead of compiling into dead code. */
int main(void) {
  try {
    throw 7;
  } catch (Exception first) {
    print("first", first.code);
  } catch (Exception second) {
    print("second", second.code);
  }
  return 0;
}
