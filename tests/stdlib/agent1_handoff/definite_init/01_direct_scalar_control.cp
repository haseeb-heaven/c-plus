// EXPECT: REJECT
// Positive control: a scalar read with no initialization on any path.
// The C+ checker already emits a tagged [CPLUS-LIFETIME] uninitialized
// diagnostic for this direct case.
#include "cplus/cplus.h"

int main(void) {
    int count;
    return count;
}
