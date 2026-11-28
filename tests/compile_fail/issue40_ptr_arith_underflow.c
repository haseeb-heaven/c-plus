/* Issue #40: raw pointer arithmetic followed by a separate dereference
 * bypasses the bounds instrumentation that array subscripts receive.  The
 * negative offset corrupts stack memory before `array`. */
#include <stdio.h>

int main(int argc, char **argv) {
    int canary = 12345;
    int array[4] = {1, 2, 3, 4};
    int offset = -1;
    if (argc > 100) offset = 0;

    int *p = array;
    p = p + offset;
    *p = 99999;
    printf("[PTR UNDERFLOW] Wrote via pointer arithmetic underflow: %d\n", *p);
    (void)canary;
    (void)argv;
    return 0;
}
