/*
 * Unit: checked `cplus_fclose_s` lifecycle.
 *
 * Maps to catalogue:
 *   bug47  - fclose(NULL) must be a defined error, never UB.
 *   bug48  - double fclose must be a defined error, never UB.
 *   bug15 / bug135 - FILE lifecycle: the owned handle location must be cleared
 *                    on close so it cannot be closed twice, and must be
 *                    reusable for the next stream lifecycle.
 *   bug110/bug111/bug112 - stream buffer lifecycle: a stream must be closed
 *                    exactly once so its stdio buffer is released once.
 */
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include "safe_io.h"
#include "safe_error.h"
#include "safe_log.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);

    const char *path = "/tmp/cplus_iofam_fclose.tmp";
    FILE *f;

    /* ---- 1. bug47: NULL handle pointer -> EINVAL (defined, no deref) ---- */
    assert(cplus_fclose_s(NULL) == CPLUS_SAFE_EINVAL);

    /* ---- 2. a pointer-to-NULL (nothing owned) is EINVAL, not OK ---- */
    f = NULL;
    assert(cplus_fclose_s(&f) == CPLUS_SAFE_EINVAL);

    /* ---- 3-7. open once, close exactly once; the owned slot is cleared ---- */
    assert(cplus_fopen_s(&f, path, "wb+") == CPLUS_SAFE_OK);
    assert(f != NULL);
    assert(cplus_fclose_s(&f) == CPLUS_SAFE_OK);
    assert(f == NULL);                                       /* bug15: no stale handle */
    /* ---- 8. bug48: closing the same POM again -> EINVAL, never UB ---- */
    assert(cplus_fclose_s(&f) == CPLUS_SAFE_EINVAL);

    /* ---- 9-12. raw handle copy: the owned POM is nulled, so a POM-based
     * double-close is impossible; an unowned raw copy still holds the old
     * address, which we drop without use (caller responsibility, bug15/135). */
    assert(cplus_fopen_s(&f, path, "wb") == CPLUS_SAFE_OK);
    {
        FILE *copy = f;                                      /* raw copy of the handle */
        assert(cplus_fclose_s(&f) == CPLUS_SAFE_OK);
        assert(f == NULL);                                   /* owned slot cleared */
        assert(copy != NULL);                                /* unowned copy untouched */
        copy = NULL;                                         /* drop the stale copy */
    }

    /* ---- 13-15. the POM is reusable across stream lifecycles (bug110-112:
     * each stream is opened and closed once, releasing its buffer once). ---- */
    assert(cplus_fopen_s(&f, path, "ab") == CPLUS_SAFE_OK);
    assert(cplus_fclose_s(&f) == CPLUS_SAFE_OK);
    assert(f == NULL);

    /* ---- 16-17. still EINVAL after the second lifecycle; NULL stays EINVAL ---- */
    assert(cplus_fclose_s(&f) == CPLUS_SAFE_EINVAL);
    assert(cplus_fclose_s(NULL) == CPLUS_SAFE_EINVAL);

    remove(path);
    puts("test_iofam_fclose: ok");
    return 0;
}
