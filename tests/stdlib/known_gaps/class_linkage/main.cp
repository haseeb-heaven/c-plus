/* Driver for the class-linkage reproduction (expected to fail to link). */
#include <stdio.h>
#include "cplus/cplus.h"

int gap_a(void);
int gap_b(void);

int main(void) {
    print("gap", gap_a(), gap_b());
    return 0;
}
