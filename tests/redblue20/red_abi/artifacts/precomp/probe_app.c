#include <stdio.h>
#include <stdlib.h>
#include "probe_lib.h"
int main(void) {
    char buf[16];
    int r = red_probe_add(2, 3);
    size_t n = red_probe_copy(buf, "ok");
    void *p = red_probe_alloc(8);
    printf("r=%d n=%zu p=%d\n", r, n, p != 0);
    free(p);
    return 0;
}
