#include <stdio.h>
static void swap(int **pp) {
    int *t = pp[0]; pp[0] = pp[1]; pp[1] = t;
}
int main(void) {
    int a = 5555, b = 21;
    int *pa = &a, *pb = &b;
    int *table[2] = {pa, pb};
    swap(table);
    printf("a=%d b=%d first=%d second=%d\n", a, b, *table[0], *table[1]);
    return 0;
}
