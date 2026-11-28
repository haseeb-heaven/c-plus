/* Fixture for the optional -fcplus-safe-gc driver smoke test.
 * Returns 0 only when the driver injected the epoch-reclamation constructor. */
#include "cplus/cplus.h"

int main(void) {
  return cplus_reclaim_enabled() ? 0 : 1;
}
