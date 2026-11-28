/* Bug shape: "adjacent" OOB read -- the loop is bounded by len but the
 * subscript adds an offset, so the last iteration reads one element past the
 * end of the array (arr[4] when arr has 4 elements). */
#include <stdio.h>

int main(void) {
    int arr[] = { 55, 66, 77, 44 };
    int len = 4;
    for (int idx = 0; idx < (int)len; idx++)
        printf("%d\n", arr[idx + 1]); /* arr[4] OOB */
    return 0;
}
