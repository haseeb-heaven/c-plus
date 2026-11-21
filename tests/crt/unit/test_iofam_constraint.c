/*
 * Unit: safeclib constraint-handler mode via
 *   cplus_safe_set_constraint_mode / cplus_safe_get_constraint_mode.
 *
 * Contract asserted here:
 *   - IGNORE (0) and ABORT (1) round-trip through get/set.
 *   - Any out-of-range value falls back to IGNORE (defined clamp, never a
 *     corrupt mode value).
 *   - In IGNORE mode a constraint violation is reported through the API return
 *     value and execution continues (no abort), and the mode is unchanged.
 */
#include <assert.h>
#include <stdio.h>
#include "safe_error.h"
#include "safe_log.h"
#include "safe_memory.h"
#include "safe_string.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);

    /* ---- 1-3. default and explicit IGNORE round-trip ---- */
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    assert(cplus_safe_get_constraint_mode() == 0);

    /* ---- 4-5. ABORT sets and reads back ---- */
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_ABORT);
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_ABORT);
    assert(cplus_safe_get_constraint_mode() == 1);

    /* ---- 6. back to IGNORE ---- */
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* ---- 7-12. numeric enum values are stable and distinct ---- */
    assert(CPLUS_SAFE_CONSTRAINT_IGNORE == 0);
    assert(CPLUS_SAFE_CONSTRAINT_ABORT == 1);
    assert(CPLUS_SAFE_CONSTRAINT_IGNORE != CPLUS_SAFE_CONSTRAINT_ABORT);

    /* ---- 13-20. out-of-range values fall back to IGNORE ---- */
    cplus_safe_set_constraint_mode((cplus_safe_constraint_mode)2);
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_safe_set_constraint_mode((cplus_safe_constraint_mode)99);
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_safe_set_constraint_mode((cplus_safe_constraint_mode)-1);
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_safe_set_constraint_mode((cplus_safe_constraint_mode)1000);
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* ---- 21-28. repeated toggling stays stable ---- */
    {
        int i;
        for (i = 0; i < 4; ++i) {
            cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_ABORT);
            assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_ABORT);
            cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
            assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
        }
    }

    /* ---- 29-32. In IGNORE mode a violation is a return value, not an abort;
     * the mode is unchanged afterwards. ---- */
    {
        char d[4] = "abc";
        char s[4] = "hi";
        assert(cplus_memcpy_s(d, sizeof d, NULL, 1) != 0);   /* null src -> error */
        assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
        assert(cplus_strcpy_s(s, sizeof s, NULL) != 0);      /* null src -> error */
        assert(s[0] == '\0');                                /* dest zeroed, no abort */
    }

    /* ---- 33. leave global state at the default for other tests ---- */
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);

    puts("test_iofam_constraint: ok");
    return 0;
}
