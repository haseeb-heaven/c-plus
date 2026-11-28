/* Positive control: reassigning the owner itself makes it live again. */
#include <stdlib.h>

int main(void) {
  int *p = (int *)malloc(sizeof(int));
  free(p);
  p = (int *)malloc(sizeof(int));
  *p = 1;
  free(p);
  return 0;
}
