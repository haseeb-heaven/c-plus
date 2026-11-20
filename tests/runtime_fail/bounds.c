#include <stdio.h>

/* A constant bad index is rejected at compile time.  This test exercises
 * the other half of the safety model: an index that is only known at run
 * time must abort safely. */
static volatile int bad_index = 7;

int main(void) {
    int values[4];
    int i;
    for (i = 0; i < 4; i++)
        values[i] = i;
    printf("in range %d\n", values[3]);
    printf("out of range %d\n", values[bad_index]);
    return 0;
}
