/* Out-of-bounds case for the -fcplus-disjoint-bounds driver smoke test.
 * The index comes from argv so the static checker cannot constant-fold it;
 * the injected cplus_shadow_slot_check() must trap with exit code 101. */
#include <stdlib.h>
int main(int argc, char **argv) {
  int a[4] = {0, 1, 2, 3};
  int i = argc > 1 ? atoi(argv[1]) : 8;
  return a[i];
}
