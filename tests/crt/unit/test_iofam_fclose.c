/* Unit: checked `cplus_fclose_s` lifecycle. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include "safe_io.h"
#include "safe_error.h"
#include "safe_log.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    const char *path = "/tmp/cplus_iofam_fclose.tmp";
    FILE *f;

    errno = 0;
    assert(cplus_fclose_s(NULL) == EOF && errno == EINVAL);

    f = cplus_fopen_s(path, "wb+");
    assert(f != NULL);
    assert(cplus_fclose_s(f) == 0);
    errno = 0;
    assert(cplus_fclose_s(f) == EOF && errno == EINVAL); /* double close refused */

    f = cplus_fopen_s(path, "ab");
    assert(f != NULL);
    assert(cplus_fclose_s(f) == 0);
    assert(cplus_fclose_s(NULL) == EOF);

    remove(path);
    puts("test_iofam_fclose: ok");
    return 0;
}
