#include <stddef.h>
extern void *cplus_register_allocation(void *, size_t, const char *, int);
extern void cplus_free_registered(void *, const char *, int);
extern void *cplus_realloc_registered(void *, void *, size_t, const char *, int);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/libc_valid.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
  int *p = cplus_register_allocation((malloc(sizeof(int) * 4)), 16, __FILE__, __LINE__);
  if (!p) return 1;
  for (int i = 0; i < 4; i++) p[i] = i;
  int *q = cplus_realloc_registered((p), (realloc(p, sizeof(int) * 8)), 32, __FILE__, __LINE__);
  if (!q) { cplus_free_registered(p, __FILE__, __LINE__); return 2; }  // preserve original on realloc failure
  cplus_free_registered(q, __FILE__, __LINE__);
  char dst[8];
  memcpy(dst, "hello", 6);
  strcpy(dst, "abc");
  printf("%s\n", dst);
  return strcmp(dst, "abc") == 0 ? 0 : 3;
}
