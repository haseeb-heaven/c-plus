/* Bug 325: returning the address of a VLA element; the array dies on return. */
#include <stddef.h>

int *return_vla(size_t n) {
  int row[n];
  row[0] = 1;
  return row;
}

int main(void) { return 0; }
