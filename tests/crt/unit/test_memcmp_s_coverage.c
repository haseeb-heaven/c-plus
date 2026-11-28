#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_error.h"
#include "safe_log.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_ERROR);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* 1. equal buffers => diff 0 */
    int diff = 99;
    assert(cplus_memcmp_s("abc", 3, "abc", 3, &diff) == 0);
    assert(diff == 0);

    /* 2. first buffer less => diff -1 */
    diff = 99;
    assert(cplus_memcmp_s("abc", 3, "abd", 3, &diff) == 0);
    assert(diff == -1);

    /* 3. first buffer greater => diff 1 */
    diff = 99;
    assert(cplus_memcmp_s("abe", 3, "abd", 3, &diff) == 0);
    assert(diff == 1);

    /* 4. same pointer is treated as equal */
    char same[4] = { 1, 2, 3, 4 };
    diff = 99;
    assert(cplus_memcmp_s(same, 4, same, 4, &diff) == 0);
    assert(diff == 0);

    /* 5. embedded null bytes compared as memory */
    char n1[4] = { 'a', '\0', 'b', 'c' };
    char n2[4] = { 'a', '\0', 'b', 'd' };
    diff = 0;
    assert(cplus_memcmp_s(n1, 4, n2, 4, &diff) == 0);
    assert(diff == -1);

    /* 6. NULL dest rejected, diff set to -1 */
    diff = 77;
    assert(cplus_memcmp_s(NULL, 3, "abc", 3, &diff) != 0);
    assert(diff == -1);

    /* 7. NULL src rejected */
    diff = 77;
    assert(cplus_memcmp_s("abc", 3, NULL, 3, &diff) != 0);
    assert(diff == -1);

    /* 8. NULL diff pointer rejected */
    assert(cplus_memcmp_s("abc", 3, "abc", 3, NULL) != 0);

    /* 9. dmax == 0 rejected */
    diff = 77;
    assert(cplus_memcmp_s("abc", 0, "abc", 3, &diff) != 0);
    assert(diff == -1);

    /* 10. slen == 0 rejected */
    diff = 77;
    assert(cplus_memcmp_s("abc", 3, "abc", 0, &diff) != 0);
    assert(diff == -1);

    /* 11. slen > dmax rejected (ESNOSPC) */
    diff = 77;
    assert(cplus_memcmp_s("ab", 2, "abc", 3, &diff) != 0);
    assert(diff == -1);

    /* 12. dmax > RSIZE_MAX_MEM rejected */
    diff = 77;
    assert(cplus_memcmp_s("abc", RSIZE_MAX_MEM + 1, "abc", 3, &diff) != 0);
    assert(diff == -1);

    return 0;
}
