#include <stdio.h>
#define WANT 1
int main(void) {
#if 0
#elifdef WANT
  puts("elifdef-taken");
#else
  puts("elifdef-else");
#endif
  return 0;
}
