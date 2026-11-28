/* Positive control: reassigning the alias itself makes it live again. */
#include <stdlib.h>

int main(void) {
  int *p = (int *)malloc(sizeof(int));
  int *q = p;
  free(p);
  p = NULL;
  q = (int *)malloc(sizeof(int));
  *q = 42;
  free(q);
  return 0;
}
