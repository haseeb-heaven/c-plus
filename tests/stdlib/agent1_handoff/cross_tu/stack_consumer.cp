/* Case C -- consumer TU: uses the borrowed pointer returned by the other TU. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stack_owner.hp"

int main(void) {
    const char *p = produce_stack_view();
    printf("%s\n", p);
    return 0;
}
