#include <stdio.h>

enum Color { RED, GREEN = 5, BLUE, MAX = 10 };

int main(void) {
    printf("enum=%d %d %d %d\n", RED, GREEN, BLUE, MAX);

    int x = -1;
    unsigned u = (unsigned)x;
    printf("cast_u=%u\n", u);
    printf("back=%d\n", (int)u);

    char c = (char)300;
    printf("c=%d\n", c);
    return 0;
}
