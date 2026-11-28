#include <stdio.h>

int main(void) {
    unsigned u = 5;
    int i = -1;

    printf("t1=%u\n", 1 ? i : u);
    printf("t2=%u\n", 0 ? i : u);

    double d = 2.5;
    int k = 3;
    printf("t3=%.2f\n", 1 ? d : k);
    printf("t4=%.2f\n", 0 ? d : k);
    return 0;
}
