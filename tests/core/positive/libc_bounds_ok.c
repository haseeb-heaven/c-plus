#include <stdio.h>
#include <string.h>
int main(void) {
  char d[16] = "a"; strcat(d, "bc");
  char e[8]; snprintf(e, sizeof(e), "%s", "abc");
  char f[8]; strncpy(f, "abc", 4);
  char s[4] = {0}; memcpy(e, s, 4);
  return d[0] == 'a' ? 0 : 1;
}
