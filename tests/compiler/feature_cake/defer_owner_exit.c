/* Defer still lowers cleanly alongside an owned allocation. */
#include <stdio.h>
#include <stdlib.h>
#include "cplus/cplus.h"

int main(void) {
  int *owner = (int *)cplus_alloc(sizeof(int));
  if (!owner) return 1;
  *owner = 7;
  defer printf("CLEAN\n");
  int ok = *owner == 7;
  cplus_release(owner);
  return ok ? 0 : 1;
}
