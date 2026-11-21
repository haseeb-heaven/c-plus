#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
  int *p = malloc(sizeof(int) * 4);
  if (!p) return 1;
  for (int i = 0; i < 4; i++) p[i] = i;
  int *q = realloc(p, sizeof(int) * 8);
  if (!q) return 2;
  free(q);
  char dst[8];
  memcpy(dst, "hello", 6);
  strcpy(dst, "abc");
  printf("%s\n", dst);
  return strcmp(dst, "abc") == 0 ? 0 : 3;
}
