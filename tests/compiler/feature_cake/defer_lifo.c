/* Defer cleanup runs in reverse declaration order on normal fallthrough. */
#include <stdio.h>

static void mark(int n) { printf("D%d ", n); }

int main(void) {
  defer mark(1);
  defer mark(2);
  defer { mark(3); mark(4); }
  mark(5);
  return 0;
}
