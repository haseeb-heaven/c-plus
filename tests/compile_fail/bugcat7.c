/* Bug 344: alloca storage treated as an owned object; the pointer must not
 * escape the frame as if it were managed memory. */
#include <alloca.h>
#include <stddef.h>
#include <stdio.h>

void use_alloca(size_t n) {
  int *obj = (int *)alloca(n * sizeof(int));
  obj[0] = 1;
  printf("%d\n", obj[0]);
}

int main(void) { return 0; }
