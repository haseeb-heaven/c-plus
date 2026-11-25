/* Gate 02: runtime/dynamic array index out of bounds (volatile index 7 into length 3).
 * Expected outcome class: not_proven (runner sees exit 0; the OOB read is silent).
 * C+ note: no checked API validates a raw stack subscript. UBSan (not C+) would
 * report "index 7 out of bounds for type 'int[3]'" at runtime, but this runner
 * builds plain C99 with no sanitizers. Nothing detects the bug here.
 */
#include <stdio.h>

int main(void) {
    int buf[3] = { 10, 20, 30 };
    volatile int idx = 7;
    int v = buf[idx];
    printf("gate02: v=%d (volatile index %d into length 3)\n", v, idx);
    return 0;
}
