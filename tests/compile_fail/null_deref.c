#include <stdio.h>

struct Point { int x; int y; };

int main(void) {
    struct Point *p = NULL;
    printf("%d\n", p->x);
    return 0;
}
