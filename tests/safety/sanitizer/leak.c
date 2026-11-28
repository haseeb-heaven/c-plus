#include <stdlib.h>
int main(void) {
  char *leaked = malloc(32);
  if (!leaked) return 2;
  leaked[0] = 1;
  return 0;
}
