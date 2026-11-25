// OP: unchecked raw malloc used with a stdlib API.
// The destination is a raw malloc() block, not a stdlib/cplus managed
// allocation, so its provenance and capacity cannot be proven by the checked
// API. A runtime-selected length (not a literal) is passed as the capacity.
#include "cplus/cplus.h"
#include <stdlib.h>
#include "../../../../stdlib/safe/safe_memory.h"

int main(int argc, char **argv) {
    char *raw = (char *)malloc(16);   /* raw allocator, unknown provenance */
    size_t claimed = (size_t)argc + 12; /* caller-asserted, unproven */
    errno_t rc;
    if (raw == NULL)
        return 1;
    (void)argv;
    rc = cplus_memset_s(raw, claimed, 0, claimed); /* stdlib checked API */
    free(raw);
    return rc != 0;
}
