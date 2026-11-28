/* issue #180: raw allocation is still live at the only function exit. */
#include <stdlib.h>
void f(void) {
  int *p = malloc(4 * sizeof(int));
  (void)p;
}
int main(void) { f(); return 0; }
