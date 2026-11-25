#include <stdio.h>

int main(void) {
    int n = 4;
    int a[n];
    for (int i = 0; i < n; i++) {
        a[i] = i * i;
    }
    printf("sz=%zu\n", sizeof(a));
    printf("v=%d %d %d %d\n", a[0], a[1], a[2], a[3]);
    return 0;
}
