#include <stdlib.h>
static void consume(int *p) { free(p); }
int main(void) {
  int *p = malloc(sizeof(int));
  consume(p);
  consume(p);  // NOSONAR intentional negative safety fixture
  return 0;
}
