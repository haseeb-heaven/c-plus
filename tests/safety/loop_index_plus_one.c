#include <stdio.h>
int main(void) { int arr[] = {55, 66, 77, 44}; size_t len = sizeof(arr)/sizeof(arr[0]);
  for (int idx=0; idx < (int)len; idx++) printf("%d\n", arr[idx+1]); return 0; }
