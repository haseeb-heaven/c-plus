#include <stdio.h>
int main(void) {
  char input[64];
  fgets(input, sizeof(input), stdin);
  printf(input);          // CWE-134
  return 0;
}
