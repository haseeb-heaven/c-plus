#include "cplus/cplus.h"
int main(void) {
  int local = 1;
  cplus_release(&local);
  return 0;
}
