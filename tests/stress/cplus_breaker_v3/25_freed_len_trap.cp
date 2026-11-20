#include "cplus/cplus.h"
int main(void) {
  int *p = cplus_new_array(int, 4);
  cplus_release(p);
  print("LEN", p.len);
  return 0;
}
