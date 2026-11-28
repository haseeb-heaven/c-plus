#include <stdio.h>

struct Q { int x, y; };

int main(void) {
    int *p = (int[]){3, 4, 5};
    printf("lit=%d %d %d\n", p[0], p[1], p[2]);

    struct Q q = (struct Q){7, 8};
    printf("q=%d %d\n", q.x, q.y);

    int sum = 0;
    for (int i = 0; i < 3; i++) {
        sum += ((int[]){1, 2, 3})[i];
    }
    printf("sum=%d\n", sum);
    return 0;
}
