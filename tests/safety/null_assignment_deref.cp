#include <stdio.h>
#include "cplus/cplus.h"
#include <stdlib.h>
int main(void) {
  int *arr = malloc(sizeof(int) * 5);
  int *ptr = arr;
  ptr = (int*)NULL;
  memset(arr, 0x7F, sizeof(int) * 5);
  printf("Ptr is %d\n", *ptr);
  return 0;
}
