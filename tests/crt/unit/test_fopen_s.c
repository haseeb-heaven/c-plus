#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include "safe_io.h"
#include "safe_error.h"
int main(void) {
    FILE *f = NULL;
    assert(cplus_fopen_s(&f, "/tmp/cplus_unit_fopen.tmp", "wb") == CPLUS_SAFE_OK);
    assert(f != NULL);
    cplus_fclose_s(&f);
    assert(cplus_fopen_s(&f, "/no/such/cplus", "rb") != CPLUS_SAFE_OK);
    assert(f == NULL);
    assert(cplus_fopen_s(NULL, "/tmp/x", "rb") == CPLUS_SAFE_EINVAL);
    unlink("/tmp/cplus_unit_fopen.tmp");
    puts("unit fopen_s: ok");
    return 0;
}
