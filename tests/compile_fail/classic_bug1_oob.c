/* Bug 1: buffer overflow / out-of-bounds index. */
#include <stdio.h>
int main(void) {
    int numbers[3] = {10, 20, 30};
    numbers[5] = 99;
    printf("%d\n", numbers[5]);
    return 0;
}
