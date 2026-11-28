/* The user's adjacent-OOB program, built recoverable: must report, not abort. */
#include <stdio.h>
int main(void) {
    int arr[] = {55, 66, 77, 44};
    size_t len = sizeof(arr) / sizeof(arr[0]);
    for (int idx = 0; idx < (int)len; idx++) {
        printf("%d\n", arr[idx + 1]);   /* arr[4] OOB on final iteration */
    }
    return 0;
}
