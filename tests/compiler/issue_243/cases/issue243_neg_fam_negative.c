#include <stdlib.h>

struct Bag { int n; int items[]; };

int main(void) {
  struct Bag *b = (struct Bag *)malloc(sizeof(struct Bag) + 4 * sizeof(int));
  int v = b->items[-1];
  free(b);
  return v;
}
