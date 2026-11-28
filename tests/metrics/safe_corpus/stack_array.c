#include <stddef.h>
int main(void) { int st[8]; size_t top=0;
  for (int i=0;i<8;i++) st[top++] = i*i;
  int sum=0; while (top>0) sum += st[--top]; return sum==140 ? 0 : 1; }
