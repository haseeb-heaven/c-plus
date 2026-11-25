#include <stddef.h>
int main(void) { int q[8]; size_t head=0,tail=0;
  for (int i=0;i<4;i++) q[tail++]=i;
  int sum=0; while (head<tail) sum+=q[head++]; return sum==6 ? 0 : 1; }
