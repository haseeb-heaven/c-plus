#include <stdio.h>
#include <stdlib.h>
static void set_ptr(int **q) { *q = malloc(sizeof(int)); }
int main(void) {
  int *p = NULL;
  set_ptr(&p);
  if (!p) return 1;
  *p = 7;
  printf("%d\n", *p);
  free(p);
  return 0;
}
