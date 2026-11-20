#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp(const void *a, const void *b) {
    int x = *(const int *)a;
    int y = *(const int *)b;
    return (x > y) - (x < y);
}

int main(void) {
    int values[] = {5, 1, 4, 2, 3};
    qsort(values, 5, sizeof(int), cmp);
    printf("%d %d %d %d %d\n", values[0], values[1], values[2], values[3],
           values[4]);
    printf("len=%d\n", (int)strlen("hello"));
    return 0;
}
