/*
 * Unit: checked `cplus_fopen_s` argument and path contract.
 *
 * Maps to catalogue bug15/bug135 (FILE lifecycle: open must be paired with a
 * close, and a failed open must leave *no* owned handle behind) and sets up
 * the bug47/bug48 lifecycle exercised in test_iofam_fclose.c.
 *
 * Contract asserted here:
 *   - NULL out-parameter           -> CPLUS_SAFE_EINVAL
 *   - NULL name / NULL mode        -> CPLUS_SAFE_EINVAL, out zeroed
 *   - missing path                 -> non-OK, out zeroed (never a dangling FILE*)
 *   - successful open + close       -> CPLUS_SAFE_OK (round-trip read-back)
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "safe_io.h"
#include "safe_error.h"
#include "safe_log.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);

    const char *path = "/tmp/cplus_iofam_fopen.tmp";
    static int dummy;                        /* never a real FILE; sentinel only */
    FILE *f;

    /* ---- 1. NULL out-parameter guard ---- */
    assert(cplus_fopen_s(NULL, path, "wb") == CPLUS_SAFE_EINVAL);

    /* ---- 2-3. NULL name -> EINVAL and out is zeroed from a non-null sentinel ---- */
    f = (FILE *)&dummy;
    assert(cplus_fopen_s(&f, NULL, "wb") == CPLUS_SAFE_EINVAL);
    assert(f == NULL);

    /* ---- 4-5. NULL mode -> EINVAL and out is zeroed ---- */
    f = (FILE *)&dummy;
    assert(cplus_fopen_s(&f, path, NULL) == CPLUS_SAFE_EINVAL);
    assert(f == NULL);

    /* ---- 6-7. missing path -> non-OK, out is zeroed (no dangling handle) ---- */
    f = (FILE *)&dummy;
    assert(cplus_fopen_s(&f, "/no/such/cplus/iofam/path", "rb") != CPLUS_SAFE_OK);
    assert(f == NULL);

    /* ---- 8-10. success: open and actually write through the returned handle ---- */
    assert(cplus_fopen_s(&f, path, "wb") == CPLUS_SAFE_OK);
    assert(f != NULL);
    assert(fwrite("hi", 1, 2, f) == 2);

    /* ---- 11-12. close succeeds and clears the owned handle (bug15/bug135) ---- */
    assert(cplus_fclose_s(&f) == CPLUS_SAFE_OK);
    assert(f == NULL);

    /* ---- 13-16. reopen + read-back proves the lifecycle produced a real file ---- */
    assert(cplus_fopen_s(&f, path, "rb") == CPLUS_SAFE_OK);
    {
        char buf[4] = { 0, 0, 0, 0 };
        assert(fread(buf, 1, 2, f) == 2);
        assert(buf[0] == 'h' && buf[1] == 'i' && buf[2] == '\0');
    }
    assert(cplus_fclose_s(&f) == CPLUS_SAFE_OK);
    assert(f == NULL);

    /* ---- 17-18. empty mode string is rejected by the platform open ---- */
    f = (FILE *)&dummy;
    assert(cplus_fopen_s(&f, path, "") != CPLUS_SAFE_OK);
    assert(f == NULL);

    remove(path);
    puts("test_iofam_fopen: ok");
    return 0;
}
