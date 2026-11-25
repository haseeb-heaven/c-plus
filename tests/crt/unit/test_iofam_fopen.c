/* Unit: checked `cplus_fopen_s` argument and path contract. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "safe_io.h"
#include "safe_error.h"
#include "safe_log.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    const char *path = "/tmp/cplus_iofam_fopen.tmp";
    FILE *f;

    errno = 0;
    assert(cplus_fopen_s(NULL, "wb") == NULL && errno == EINVAL);
    errno = 0;
    assert(cplus_fopen_s(path, NULL) == NULL && errno == EINVAL);
    assert(cplus_fopen_s("/no/such/cplus/iofam/path", "rb") == NULL);

    f = cplus_fopen_s(path, "wb");
    assert(f != NULL);
    assert(fwrite("hi", 1, 2, f) == 2);
    assert(cplus_fclose_s(f) == 0);

    f = cplus_fopen_s(path, "rb");
    assert(f != NULL);
    {
        char buf[4] = {0, 0, 0, 0};
        assert(fread(buf, 1, 2, f) == 2);
        assert(buf[0] == 'h' && buf[1] == 'i' && buf[2] == '\0');
    }
    assert(cplus_fclose_s(f) == 0);
    assert(cplus_fopen_s(path, "") == NULL);

    remove(path);
    puts("test_iofam_fopen: ok");
    return 0;
}
