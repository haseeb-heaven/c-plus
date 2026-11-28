/* issue #226: a call summary records slot 1 for helper, but this cast
 * function-pointer call supplies only one argument. The checker must not
 * index call->getArg(1) out of bounds (no crash, no false diagnostic). */
#include <stdlib.h>

void helper(int *a, int *b) { free(b); }

void caller(int *x) {
  void (*fp)() = (void (*)())helper;
  fp(x);
}
