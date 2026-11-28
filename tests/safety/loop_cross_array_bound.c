#include <stdio.h>
int main(void) { int source[]={1,2,3,4,5}; int destination[]={10,20,30};
  size_t source_len = sizeof(source)/sizeof(source[0]);
  for (size_t index=0; index < source_len; index++) printf("%d\n", destination[index]); return 0; }
