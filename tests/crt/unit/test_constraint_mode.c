#include <assert.h>
#include <string.h>
#include "safe_error.h"
#include "safe_memory.h"
#include "safe_log.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_ERROR);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    char d[4];
    assert(cplus_memcpy_s(d, 4, NULL, 1) != 0); /* should return error, not abort */
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_ABORT);
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_ABORT);
    /* reset to ignore for other tests */
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    return 0;
}
