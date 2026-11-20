#include <stdio.h>

int main(void) {
    int arr[] = {10, 20, 30};
    int fixed[5];
    int i;

    printf("arr=%d fixed=%d\n", arr.len, fixed.len);
    for (i = 0; i < arr.len; i++)
        printf("%d ", arr[i]);
    printf("\n");
    return 0;
}
