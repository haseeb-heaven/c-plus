#include <stddef.h>
static void write_at(char *base, size_t off) {
  int *p = (int *)(base + off);
  *p = 1;
}
int main(void) {
  char buffer[16] = {0};
  volatile size_t off = 1;
  write_at(buffer, off);
  return buffer[0];
}
