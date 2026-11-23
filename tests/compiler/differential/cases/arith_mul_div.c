#include <stdio.h>
int main(void) {
    unsigned a = 770u, b = 822u, c = 16388u, d = 968u;
    printf("mul=%u div=%u mod=%u\n", a * b, c / d, c % d);
    printf("wrap=%u\n", a * b * c);
    return 0;
}
