#include "int_header.h"
void int_fill(unsigned char *buffer, size_t count) {
  for (size_t i = 0; i < count; ++i) buffer[i] = (unsigned char)(i + 1);
}
int int_sum(const unsigned char *buffer, size_t count) {
  int total = 0;
  for (size_t i = 0; i < count; ++i) total += buffer[i];
  return total;
}
