#include <stdio.h>
#include <stdlib.h>

/* Real-world style: qsort comparators for scalar and record arrays. */
typedef struct { int key; const char *name; } Item;
static int cmp_item(const void *a, const void *b) {
  int x = ((const Item *)a)->key; int y = ((const Item *)b)->key;
  if (x > y) return 1; if (x < y) return -1; return 0;
}
static int cmp_int(const void *a, const void *b) {
  int x = *(const int *)a; int y = *(const int *)b; return (x > y) - (x < y);
}
int main(void) {
  Item items[4]; int nums[5];
  items[0].key = 3; items[0].name = "c";
  items[1].key = 1; items[1].name = "a";
  items[2].key = 4; items[2].name = "d";
  items[3].key = 2; items[3].name = "b";
  qsort(items, 4, sizeof(Item), cmp_item);
  nums[0] = 5; nums[1] = 3; nums[2] = 9; nums[3] = 1; nums[4] = 4;
  qsort(nums, 5, sizeof(int), cmp_int);
  printf("items=%s%s%s%s nums=%d%d%d%d%d\n",
         items[0].name, items[1].name, items[2].name, items[3].name,
         nums[0], nums[1], nums[2], nums[3], nums[4]);
  return 0;
}
