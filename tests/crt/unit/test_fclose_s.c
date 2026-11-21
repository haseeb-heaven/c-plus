#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include "safe_io.h"
#include "safe_error.h"
int main(void) {
    FILE *f = NULL;
    assert(cplus_fopen_s(&f, "/tmp/cplus_unit_fclose.tmp", "wb") == CPLUS_SAFE_OK);
    assert(cplus_fclose_s(&f) == CPLUS_SAFE_OK && f == NULL);
    assert(cplus_fclose_s(&f) == CPLUS_SAFE_EINVAL);
    unlink("/tmp/cplus_unit_fclose.tmp");
    puts("unit fclose_s: ok");
    return 0;
}
