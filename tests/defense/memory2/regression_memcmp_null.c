/* Minimal regression: cplus_memcmp_s_auto must refuse (error sentinel -1)
 * two NULL operands with n > 0, never report "equal" (0).
 *
 * Before the fix: returns 0 (UNEXPECTED_OK / fail-open) => exit 1.
 * After the fix:  returns -1 with errno == EINVAL          => exit 0.
 */
#include "safe_memory.h"
#include "safe_runtime.h"
#include <errno.h>
#include <stdio.h>

int main(void) {
    int v;
    cplus_runtime_set_violation_trap(0);
    errno = 0;
    v = cplus_memcmp_s_auto(NULL, NULL, 8, (size_t)-1, (size_t)-1);
    if (v != -1 || errno != EINVAL) {
        fprintf(stderr,
                "FAIL: memcmp_s_auto(NULL,NULL,8) = %d errno=%d "
                "(expected -1 / EINVAL)\n", v, errno);
        return 1;
    }
    /* Do not regress the one-NULL ordering sentinels used by existing tests. */
    {
        unsigned char b[8] = {0};
        int a, c;
        errno = 0; a = cplus_memcmp_s_auto(NULL, b, 4, (size_t)-1, (size_t)-1);
        errno = 0; c = cplus_memcmp_s_auto(b, NULL, 4, (size_t)-1, (size_t)-1);
        if (!(a < 0 && c > 0)) {
            fprintf(stderr, "FAIL: one-NULL sentinels changed: %d %d\n", a, c);
            return 1;
        }
    }
    printf("PASS regression_memcmp_null\n");
    return 0;
}
