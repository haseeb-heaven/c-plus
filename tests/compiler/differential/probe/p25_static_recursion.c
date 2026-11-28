#include <stdio.h>

int fact(int n) {
    return n <= 1 ? 1 : n * fact(n - 1);
}

int main(void) {
    static int counter = 0;
    counter += 1;
    printf("fact=%d counter=%d\n", fact(6), counter);
    return 0;
}
