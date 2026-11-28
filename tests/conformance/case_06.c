/* Gate 06: ownership/lifetime (object used after its owner scope ends).
 * Expected outcome class: not_proven (runner sees exit 0 with a stale value).
 * C+ note: escaped stack addresses are a documented compiler gap -- c+ does not
 * diagnose this shape, and no libc checked API can validate a raw escaped
 * address after the frame is gone. UBSan does not detect it either.
 */
#include <stdio.h>

static int *escaped;

static void set_escaped(void) {
    int local = 42;                     /* dies when this frame returns */
    escaped = &local;
}

int main(void) {
    set_escaped();
    printf("gate06: v=%d after owner scope ended\n", *escaped);
    return 0;
}
