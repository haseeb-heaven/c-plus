/* Bug 310: returning a pointer into a local VLA whose storage dies with the
 * declaring frame. */
#include <stddef.h>

int *escape_vla_element(size_t n) {
  int buf[n];
  buf[0] = 1;
  return &buf[0];
}

int main(void) { return 0; }
