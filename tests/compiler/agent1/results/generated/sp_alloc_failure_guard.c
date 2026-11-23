#include <stddef.h>
extern void *cplus_register_allocation(void *, size_t, const char *, int);
extern void cplus_free_registered(void *, const char *, int);
extern void *cplus_realloc_registered(void *, void *, size_t, const char *, int);
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/alloc_failure_guard.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
  int *arr = (int *)cplus_register_allocation((malloc(sizeof(int) * 5)), 20, __FILE__, __LINE__);
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
  cplus_free_registered(arr, __FILE__, __LINE__);
  return 0;
}
