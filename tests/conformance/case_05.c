/* Gate 05: double free (same pointer released twice).
 * Expected outcome class: runtime_reject via checked API (free_checked_s refuses the second release).
 * C+ note: the recent-free cache is bounded; raw free(p) twice is UB and is not
 * detected by UBSan. Detection here comes from the checked API, not a sanitizer.
 */
#include <stdio.h>
#include "stdlib/safe/safe_c.h"

int main(void) {
    void *p = malloc_s(16u);
    if (p == NULL) {
        printf("gate05: allocation failed\n");
        return 0;
    }
    free_s(p);
    {
        int rc = free_checked_s(p);     /* second release of the same pointer */
        printf("gate05: second free refused rc=%d\n", rc);
        return rc == 0 ? 0 : 1;
    }
}
