#include <stddef.h>
#include <stdint.h>
extern void cplus_check_ptr_formation(const void *, size_t, int64_t, const char *, int, const char *);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/safe_memory.c"
#include <stdio.h>
#include "cplus/cplus.h"
int main(void) {
  char buf[16];
  int ok = cplus_strcpy(buf, sizeof(buf), "core") == 4;
  ok = ok && cplus_strnlen(buf, sizeof(buf)) == 4;
  ok = ok && cplus_memcpy((cplus_check_ptr_formation((const void *)(buf), (size_t)(16), (int64_t)(1), __FILE__, __LINE__, "buf + 1"), (buf + 1)), buf, 4) == (cplus_check_ptr_formation((const void *)(buf), (size_t)(16), (int64_t)(1), __FILE__, __LINE__, "buf + 1"), (buf + 1));
  ok = ok && buf[0] == 'c' && buf[1] == 'c' && buf[2] == 'o';
  ok = ok && cplus_memcpy(NULL, NULL, 0) == NULL;
  return ok ? 0 : 1;
}
