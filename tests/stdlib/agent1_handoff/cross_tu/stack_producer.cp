/* Case C -- producer TU: returns a borrowed view of a stack local. */
#include "stack_owner.hp"

const char *produce_stack_view(void) {
    char local[16] = "stack-data";
    return local;
}
