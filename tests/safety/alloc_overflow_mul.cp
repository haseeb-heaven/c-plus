#include "cplus/cplus.h"
int main(void) {
  managed int *p = cplus_new_array(int, SIZE_MAX / 2);
  if (p) cplus_release(p); /* keep the allocation-overflow runtime trap as the tested failure */
  return 0;
}
