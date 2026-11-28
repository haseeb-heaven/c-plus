#include <stdlib.h>
int main(void) {
  int *p = malloc(sizeof(int));
  if (!p) return 2;   // NULL path: no leak
  return 0;           // success path: genuine leak
}
