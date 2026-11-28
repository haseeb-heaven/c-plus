#include <stdio.h>
#include <stdarg.h>

int sum(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int s = 0;
    for (int i = 0; i < n; i++) {
        s += va_arg(ap, int);
    }
    va_end(ap);
    return s;
}

int main(void) {
    printf("sum=%d\n", sum(4, 1, 2, 3, 4));
    printf("printf=%d %s %c %.2f\n", 42, "ok", 'A', 3.14159);
    return 0;
}
