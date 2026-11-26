#include <stdio.h>

/* Issue #85: the written element type and qualifiers must survive lowering,
 * including multi-token types and pointer declarators. */
int main(void) {
  int arr[1] = {1};

  for (long x : arr)
    printf("long %zu\n", sizeof(x));
  for (unsigned int x : arr)
    printf("uint %zu\n", sizeof(x));
  for (const int x : arr)
    printf("const %zu\n", sizeof(x));
  for (auto v : arr)
    printf("auto %zu\n", sizeof(v));
  for (const auto cv : arr)
    printf("constauto %zu\n", sizeof(cv));
  return 0;
}
