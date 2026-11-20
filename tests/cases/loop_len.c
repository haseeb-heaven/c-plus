#include <stdio.h>

int main(void) {
    int arr[] = {10, 30, 50};
    auto total = 0;
    for (auto idx = 0; idx < arr.len; idx++)
        total += arr[idx];
    printf("total=%d\n", total);
    return 0;
}
