#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include "safe_io.h"
#include "safe_error.h"
int main(void) {
    FILE *f = cplus_fopen_s("/tmp/cplus_unit_fclose.tmp", "wb");
    assert(f != NULL);
    assert(cplus_fclose_s(f) == 0);
    errno = 0;
    assert(cplus_fclose_s(f) == EOF && errno == EINVAL);
    unlink("/tmp/cplus_unit_fclose.tmp");
    puts("unit fclose_s: ok");
    return 0;
}
