/* Bug shape: wrong-length index -- the loop is bounded by a *different*
 * array's length (source_len = 5) but indexes `destination`, which has only 3
 * elements, so destination[3] is OOB. */
#include <stdio.h>

int main(void) {
    int source[5] = { 1, 2, 3, 4, 5 };
    int destination[3] = { 10, 20, 30 };
    int source_len = 5;
    for (int index = 0; index < source_len; index++)
        printf("%d\n", destination[index]); /* destination[3] OOB */
    return 0;
}
