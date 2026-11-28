#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
  int *arr = (int *)malloc(1);
  printf("Arr is %p\n", (void *)arr);
  int *ptr = arr;
  memset(arr, 0, sizeof(int) * 5);
  int initial[] = {10, 40, 50, 60};
  memcpy(arr, initial, sizeof(initial));
  printf("Ptr is %d\n", *ptr);
  free(arr);
  return 0;
}
