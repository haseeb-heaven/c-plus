#include <stdio.h>

int main(void) {
    int values[4];
    int i;
    for (i = 0; i < 4; i++)
        values[i] = i;
    printf("in range %d\n", values[3]);
    printf("out of range %d\n", values[7]);
    return 0;
}
