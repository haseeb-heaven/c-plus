#include <stdio.h>
#include "cplus/cplus.h"
int main(void) {
  char buf[16];
  int ok = cplus_strcpy(buf, sizeof(buf), "core") == 4;
  ok = ok && cplus_strnlen(buf, sizeof(buf)) == 4;
  ok = ok && cplus_memcpy(buf + 1, buf, 4) == buf + 1;
  ok = ok && buf[0] == 'c' && buf[1] == 'c' && buf[2] == 'o';
  ok = ok && cplus_memcpy(NULL, NULL, 0) == NULL;
  return ok ? 0 : 1;
}
