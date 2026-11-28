#include <stdio.h>
#include "cplus/cplus.h"
int main(int argc, char **argv) {
  int *p = cplus_new_array(int, argc + 2);
  print(p.len);
  return 0;
}
