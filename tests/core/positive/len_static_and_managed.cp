#include <stdio.h>
#include "cplus/cplus.h"
int main(void) {
  int a[4] = {0};
  int *p = cplus_new_array(int, 5);
  print(a.len, p.len);
  return 0;
}
