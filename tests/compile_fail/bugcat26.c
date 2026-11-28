/* BUG 525: pointer into alloca (automatic) storage escapes its frame. */
#include <alloca.h>
#include <stddef.h>

void *bug_alloca_escape(void) {
  char *buf = (char *)alloca(64);
  // BUG: buf is stack storage that dies at return; the caller gets a dangling
  // pointer, and alloca has no failure signal or bounds checking.
  buf[0] = 'x';
  return buf;
}

int main(void) {
  (void)bug_alloca_escape();
  return 0;
}
