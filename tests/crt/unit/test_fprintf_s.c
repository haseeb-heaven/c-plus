#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include "safe_format.h"
#include "safe_io.h"
#include "safe_error.h"
int main(void) {
    FILE *f = cplus_fopen_s("/tmp/cplus_unit_fprintf.tmp", "wb+");
    assert(f != NULL);
    assert(cplus_fprintf_s(f, "ok") >= 0);
    cplus_fclose_s(f);
    unlink("/tmp/cplus_unit_fprintf.tmp");
    puts("unit fprintf_s: ok");
    return 0;
}
