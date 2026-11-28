#include <stdio.h>
#include <stddef.h>

struct M { char c; double d; short s; };

int main(void) {
    printf("i=%zu c=%zu d=%zu\n", sizeof(int), sizeof(char), sizeof(double));
    printf("m=%zu align=%zu\n", sizeof(struct M), _Alignof(struct M));

    int arr[5];
    printf("arr=%zu elem=%zu\n", sizeof(arr), sizeof(arr[0]));
    printf("p=%zu\n", sizeof(int *));
    return 0;
}
