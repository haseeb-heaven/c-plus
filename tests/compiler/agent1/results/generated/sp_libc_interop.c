#include <stddef.h>
extern void *cplus_register_allocation(void *, size_t, const char *, int);
extern void cplus_free_registered(void *, const char *, int);
extern void *cplus_realloc_registered(void *, void *, size_t, const char *, int);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/libc_interop.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
  char buf[16];
  strcpy(buf, "core");
  int *p = cplus_register_allocation((malloc(sizeof(int))), 4, __FILE__, __LINE__);
  *p = 7;
  int ok = strlen(buf) == 4 && *p == 7;
  cplus_free_registered(p, __FILE__, __LINE__);
  return ok ? 0 : 1;
}
