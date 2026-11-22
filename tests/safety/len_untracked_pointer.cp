#include <stdio.h>
#include "cplus/cplus.h"
#include <stdlib.h>
int main(void) {
  int *arr = malloc(sizeof(int) * 5);
  int *ptr = arr;
  arr[0]=10; arr[1]=20; arr[3]=30;
  print("Ptr is ", *ptr);
  print("Arr len is ", arr.len);
  return 0;
}
