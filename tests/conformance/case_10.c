/* Gate 10: uninitialized memory (struct field read before assignment).
 * Expected outcome class: not_proven (runner sees exit 0 with an indeterminate value).
 * C+ note: -Wuninitialized / UBSan do not diagnose this struct-field shape, and
 * no library API can check caller memory state. This is a genuine C+ gap, not
 * a sanitizer-only detection.
 */
#include <stdio.h>

struct point { int a; int b; };

int main(void) {
    struct point p;
    p.a = 5;                            /* p.b is never assigned */
    printf("gate10: a=%d b=%d (b uninitialized)\n", p.a, p.b);
    return 0;
}
