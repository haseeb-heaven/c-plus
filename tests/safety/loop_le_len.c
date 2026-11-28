#include <stdio.h>
int main(void) { int data[] = {10,20,30,40}; size_t len = sizeof(data)/sizeof(data[0]);
  for (size_t index=0; index <= len; index++) printf("%d\n", data[index]); return 0; }
