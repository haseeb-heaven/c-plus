/* Agent 1 issue #105 (GitHub #104): an inner catch must save the previous
 * rethrow slot before installing its own exception and restore it on every
 * exit edge, including return/break, so an outer `throw;` rethrows correctly. */
#include "cplus/cplus.h"

static void inner_return(void) {
  try {
    throw "INNER_RETURN";
  } catch (Exception inner) {
    (void)inner;
    return; /* guard cleanup must restore the saved outer exception */
  }
}

static void after_return(void) {
  try {
    throw "OUTER_RETURN";
  } catch (Exception outer) {
    (void)outer;
    inner_return();
    throw; /* must rethrow OUTER_RETURN */
  }
}

static void after_break(void) {
  try {
    throw "OUTER_BREAK";
  } catch (Exception outer) {
    (void)outer;
    for (int i = 0; i < 2; ++i) {
      try {
        throw "INNER_BREAK";
      } catch (Exception inner) {
        (void)inner;
        break; /* guard cleanup must restore the saved outer exception */
      }
    }
    throw; /* must rethrow OUTER_BREAK */
  }
}

int main(void) {
  try {
    after_return();
  } catch (Exception e) {
    print("return", e.message);
  }
  try {
    after_break();
  } catch (Exception e) {
    print("break", e.message);
  }
  return 0;
}
