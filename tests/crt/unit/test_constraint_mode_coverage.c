#include <assert.h>
#include <stddef.h>
#include <string.h>
#include "safe_error.h"
#include "safe_log.h"
#include "safe_memory.h"
#include "safe_string.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_ERROR);

    /* 1. default/ignore mode is readable and stable */
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* 2. invalid mode falls back to IGNORE */
    cplus_safe_set_constraint_mode((cplus_safe_constraint_mode)99);
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* 3. ABORT mode can be set and read back */
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_ABORT);
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_ABORT);

    /* 4. reset to IGNORE for runtime constraint checks */
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* 5. memcpy_s null dest returns error in IGNORE mode */
    char d[8] = "ok";
    assert(cplus_memcpy_s(NULL, sizeof(d), "x", 1) != 0);

    /* 6. memcpy_s null src returns error; C+ SECURITY PATCH keeps dest intact
     * (upstream's dmax-byte wipe could overrun an undersized destination). */
    char d2[8] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', '\0' };
    assert(cplus_memcpy_s(d2, sizeof(d2), NULL, 1) != 0);
    assert(d2[0] == 'A');

    /* 7. memset_s invalid args return error in IGNORE mode */
    assert(cplus_memset_s(NULL, 4, 0, 1) != 0);

    /* 8. memcmp_s constraint on null diff returns error */
    assert(cplus_memcmp_s("a", 1, "a", 1, NULL) != 0);

    /* 9. memzero_s null dest returns error */
    assert(cplus_memzero_s(NULL, 4) != 0);

    /* 10. strcpy_s constraint returns error in IGNORE mode */
    char sd[4] = "hi";
    assert(cplus_strcpy_s(sd, sizeof(sd), NULL) != 0);
    assert(sd[0] == '\0');

    /* 11. strtok_s invalid dmax pointer returns NULL */
    char tokbuf[8] = "a,b";
    char *ctx = NULL;
    assert(cplus_strtok_s(tokbuf, NULL, ",", &ctx) == NULL);

    /* 12. mode remains IGNORE after constraint violations */
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);

    return 0;
}
