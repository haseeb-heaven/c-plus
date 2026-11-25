#include <stdlib.h>
int main(void) {
  int *p = malloc(4 * sizeof(int));
  if (!p) return 2;
  for (int i = 0; i < 4; i++) p[i] = i;
  int sum = 0;
  for (int i = 0; i < 4; i++) sum += p[i];
  free(p);
  return sum == 6 ? 0 : 1;
}
