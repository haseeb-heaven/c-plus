#include "cplus/cplus.h"

/* Issue #83: a nested catch inside a handler must not overwrite the outer
 * rethrow slot, so a bare `throw;` still raises the outer exception. */
int main(void) {
  try {
    try {
      throw "OUTER_ERROR";
    } catch (Exception outer) {
      try {
        throw "INNER_ERROR";
      } catch (Exception inner) {
        (void)inner;
      }
      throw;
    }
  } catch (Exception final_ex) {
    print("Caught:", final_ex.message);
  }
  return 0;
}
