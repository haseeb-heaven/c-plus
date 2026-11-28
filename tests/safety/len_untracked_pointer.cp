#include <stdio.h>
#include "cplus/cplus.h"
#include <stdlib.h>
int main(int argc, char **argv) {
  (void)argv;
  int *arr = malloc(argc * sizeof(int));  // runtime size: no tracked length
  print("Arr len is ", arr.len);
  return 0;
}
