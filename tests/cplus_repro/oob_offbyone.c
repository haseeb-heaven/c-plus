/* Bug shape: classic off-by-one -- the loop uses `<= len` instead of `< len`,
 * so the final iteration reads data[4] when data has 4 elements. */
#include <stdio.h>

int main(void) {
    int data[] = { 10, 20, 30, 40 };
    int len = 4;
    for (int index = 0; index <= len; index++)
        printf("%d\n", data[index]); /* data[4] OOB */
    return 0;
}
