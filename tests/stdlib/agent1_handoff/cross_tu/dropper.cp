/* P1 cross-TU reproducer, case B -- releasing translation unit. */
#include "cplus/cplus.h"
#include "shared.hp"

void *shared_resource = NULL;

void release_shared_resource(void) {
    shared_resource = cplus_alloc(16);
    cplus_release(shared_resource); /* resource dropped in this TU */
}
