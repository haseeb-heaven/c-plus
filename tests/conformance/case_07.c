/* Gate 07: null/dangling pointer (NULL passed to a guarded API).
 * Expected outcome class: runtime_reject via checked API (NULL guard returns EINVAL).
 * C+ note: the guarded entry point rejects NULL with a defined error; an
 * unguarded raw dereference of NULL is still UB and is not fixed by this layer.
 */
#include <stdio.h>
#include "stdlib/safe/safe_c.h"

int main(void) {
    size_t count = 99u;
    int rc = cplus_safe_query_length(NULL, 4u, &count);
    printf("gate07: guarded query rc=%d count=%lu\n", rc, (unsigned long)count);
    return rc == 0 ? 0 : 1;
}
