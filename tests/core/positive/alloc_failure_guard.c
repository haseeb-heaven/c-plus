#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
  int *arr = (int *)malloc(sizeof(int) * 5);
  if (arr == NULL) {
    fprintf(stderr, "Allocation failed\n");
    return 1;
  }
  printf("Arr is %p\n", (void *)arr);
  int *ptr = arr;
  memset(arr, 0, sizeof(int) * 5);
  int initial[] = {10, 40, 50, 60};
  memcpy(arr, initial, sizeof(initial));
  printf("Ptr is %d\n", *ptr);
  for (int i = 0; i < 5; i++) printf("%d ", arr[i]);
  printf("\n");
  free(arr);
  return 0;
}
