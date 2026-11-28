/* Agent 1 issue #104 (GitHub #105): throw operands that contain calls must be
 * preserved and evaluated, not replaced by the hardcoded code 1. */
#include "cplus/cplus.h"

static int get_code(void) { return 7; }
static int compute(int a, int b) { return a + b; }

class Err {
  int v;
  int code(void) { return this->v; }
};

int main(void) {
  try {
    throw get_code();
  } catch (Exception e) {
    print("function", e.code);
  }
  try {
    throw compute(3, 4);
  } catch (Exception e) {
    print("compute", e.code);
  }
  Err err = {9};
  try {
    throw err.code();
  } catch (Exception e) {
    print("method", e.code);
  }
  try {
    throw (int)get_code();
  } catch (Exception e) {
    print("cast", e.code);
  }
  return 0;
}
