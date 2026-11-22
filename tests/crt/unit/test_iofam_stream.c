/* Unit: checked `cplus_fread_s` / `cplus_fwrite_s` stream buffer contract. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "safe_io.h"
#include "safe_error.h"
#include "safe_log.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    const char *path = "/tmp/cplus_iofam_stream.tmp";
    const char payload[] = "ABCDEFGH";
    FILE *f = cplus_fopen_s(path, "wb+");
    char buf[16];
    assert(f != NULL);

    assert(cplus_fwrite_s(NULL, sizeof payload, 1, 1, f) == 0);
    assert(cplus_fwrite_s(payload, sizeof payload, 1, 1, NULL) == 0);
    assert(cplus_fwrite_s(payload, sizeof payload, 0, 1, f) == 0);
    assert(cplus_fwrite_s(payload, sizeof payload, 1, 0, f) == 0);
    assert(cplus_fwrite_s(payload, 4, 1, 8, f) == 4);

    assert(cplus_fread_s(NULL, sizeof buf, 1, 1, f) == 0);
    assert(cplus_fread_s(buf, sizeof buf, 1, 1, NULL) == 0);
    assert(cplus_fread_s(buf, sizeof buf, 0, 1, f) == 0);
    assert(cplus_fread_s(buf, sizeof buf, 1, 0, f) == 0);

    rewind(f);
    {
        size_t n = cplus_fread_s(buf, 4, 1, 8, f);
        assert(n == 4 && memcmp(buf, "ABCD", 4) == 0);
    }
    rewind(f);
    {
        unsigned char raw[8] = {0};
        assert(cplus_fread_s(raw, sizeof raw, 1, 4, f) == 4);
        assert(raw[0] == 'A' && raw[2] == 'C' && raw[3] == 'D');
    }
    assert(cplus_fread_s(buf, 1, 4, 2, f) == 0);
    assert(cplus_fclose_s(f) == 0);

    remove(path);
    puts("test_iofam_stream: ok");
    return 0;
}
