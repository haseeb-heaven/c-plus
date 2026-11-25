/* Runtime print() regression: long strings, many args and nested calls.
 * Prints deterministic stdout; see run_print_test.sh for the exact gate. */
#include "cplus/cplus.h"

static int nested(int n) {
    print("PRINT nested", n);
    return n + 1;
}

int main(void) {
    char big[2000];
    int i;
    for (i = 0; i < 1999; ++i)
        big[i] = 'A';
    big[1999] = 0;
    print(big);
    print("PRINT many", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    print("PRINT outer", nested(7));
    return 0;
}
