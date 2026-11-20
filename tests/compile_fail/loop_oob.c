/* A counted loop must not read past a known array extent. */
#include <stdio.h>

int main(void) {
    int arr[] = {10, 30, 50};
    for (int idx = 0; idx < 5; idx++) {
        printf("Arr is %d\t", arr[idx]);
    }
    return 0;
}
