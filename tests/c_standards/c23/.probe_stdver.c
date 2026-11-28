#include <stdio.h>
int main(void) {
#ifndef __STDC_VERSION__
  puts("NO_STDC_VERSION");
#else
  printf("__STDC_VERSION__=%ld\n", (long)__STDC_VERSION__);
#endif
  return 0;
}
