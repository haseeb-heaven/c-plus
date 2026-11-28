#include <stdio.h>
int main(void) {
    puts("first");
    putchar('A'); putchar('\n');
    fprintf(stdout, "third=%d\n", 67295);
    printf("\nlast\n");
    return 0;
}
