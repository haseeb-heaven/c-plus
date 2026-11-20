/* NOT DETECTED: a macro that evaluates its argument twice. */
#include <stdio.h>
#define SQUARE(x) ((x) * (x))
int main(void) {
    int i = 3;
    int result = SQUARE(i++);
    printf("%d %d\n", result, i);
    return 0;
}
