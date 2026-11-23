#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/libc_interop.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
  char buf[16];
  strcpy(buf, "core");
  int *p = malloc(sizeof(int));
  *p = 7;
  int ok = strlen(buf) == 4 && *p == 7;
  free(p);
  return ok ? 0 : 1;
}