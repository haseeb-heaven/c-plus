// Positive control for case B: the same alloc/release/subscript sequence
// written in a single TU.  The C+ checker proves the use-after-free here and
// emits a tagged [CPLUS-LIFETIME] diagnostic.  This is the behaviour that the
// cross-TU split in dropper.cp + user.cp fails to reproduce.
#include "cplus/cplus.h"

int main(void) {
    void *shared_resource = cplus_alloc(16);
    cplus_release(shared_resource);
    return ((char *)shared_resource)[0];
}
