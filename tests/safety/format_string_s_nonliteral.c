#include <stdio.h>
int cplus_printf_s(const char *fmt, ...);
int main(void) {
  char input[64];
  fgets(input, sizeof(input), stdin);
  cplus_printf_s(input);  // _s suffix does not sanitise the format
  return 0;
}
