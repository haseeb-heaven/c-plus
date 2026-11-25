/* Demo H: managed allocation with a tracked length and a borrowed view.
 *
 * The allocation is released automatically at scope exit; the borrow is used
 * only while the owner is alive. */
#include <stdio.h>
#include "cplus/cplus.h"

static int sum(const int *p, size_t n) {
    int total = 0;
    size_t i;
    for (i = 0; i < n; i++)
        total += p[i];
    return total;
}

int main(void) {
    managed int *values = cplus_new_array(int, 4);
    int *borrow = values;
    int i;

    for (i = 0; i < 4; i++)
        values[i] = (i + 1) * 10;

    printf("len=%zu total=%d\n", values.len, sum(borrow, values.len));
    return 0;
}
