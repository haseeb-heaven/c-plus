#include <stdio.h>
static int read_value(int *p) { return *p; }
int main(void) {
  int x = 7;
  if (read_value(&x) != 7) return 1;
  if (read_value(&x) != 7) return 2;
  return 0;
}
