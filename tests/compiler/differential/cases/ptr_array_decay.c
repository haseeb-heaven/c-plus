#include <stdio.h>
int main(void) {
    int a[4] = {416, 374, 905, 136};
    int *p = a;
    printf("sizeof_arr=%zu sizeof_ptr=%zu\n", sizeof(a), sizeof(p));
    printf("p1=%d p3=%d\n", p[1], p[3]);
    return 0;
}
