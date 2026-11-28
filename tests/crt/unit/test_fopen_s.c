#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include "safe_io.h"
#include "safe_error.h"
int main(void) {
    FILE *f = cplus_fopen_s("/tmp/cplus_unit_fopen.tmp", "wb");
    assert(f != NULL);
    assert(cplus_fclose_s(f) == 0);
    f = cplus_fopen_s("/no/such/cplus", "rb");
    assert(f == NULL);
    errno = 0;
    assert(cplus_fopen_s(NULL, "rb") == NULL && errno == EINVAL);
    unlink("/tmp/cplus_unit_fopen.tmp");
    puts("unit fopen_s: ok");
    return 0;
}
