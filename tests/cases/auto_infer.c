#include <stdio.h>

int main(void) {
    int arr[] = {10, 20, 30};
    auto n = arr.len;
    auto total = 0;
    for (auto i = 0; i < n; i++)
        total += arr[i];
    auto p = &total;
    printf("n=%d total=%d p=%d\n", n, total, *p);
    return 0;
}
