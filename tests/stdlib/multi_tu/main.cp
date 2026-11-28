/* Multi-translation-unit test: three TUs sharing the same function-only .hp
 * header.  Function definitions are static inline, so this links cleanly. */
#include <stdio.h>
#include "cplus/cplus.h"

int part_a(void);
int part_b(void);

int main(void) {
    int a = part_a();
    int b = part_b();
    print("multi-tu", a, b);
    return (a == 0 && b == 3) ? 0 : 1;
}
