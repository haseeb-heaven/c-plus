#include <stddef.h>
int main(void) { const char *s = "hello world"; size_t n = 0;
  while (s[n] != '\0') n++; return n == 11 ? 0 : 1; }
