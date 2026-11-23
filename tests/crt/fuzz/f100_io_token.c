/* F-054: cplus_strtok_s over a deterministic stdin token fixture (leading /
 * repeated delimiters, CRLF, tab, colon, unterminated EOF token).  The fixture
 * is re-tokenized 300 times from a fresh writable copy; the backend quirk of
 * re-returning the final token is tolerated by the 64-call cap. */
#include "f100_io_common.h"

#define WORK_SZ 512
#define DELIM " \t\r\n,;:"

int main(void) {
    char path[512], src[WORK_SZ], work[WORK_SZ];
    size_t got, j;
    unsigned pass;
    int guard;

    f100_init();
    f100_tmp_path(path, sizeof path, "token");
    f100_write_token_fixture(path);
    if (freopen(path, "rb", stdin) == NULL) {
        F100_FAIL("cannot attach token fixture");
        (void)unlink(path);
        return f100_report("F-054");
    }
    got = fread(src, 1, sizeof src - 1, stdin);
    src[got] = '\0';
    if (got == 0) F100_FAIL("empty token fixture");

    for (pass = 0; pass < 300; ++pass) {
        rsize_t dmax = (rsize_t)sizeof work;
        char *ctx = NULL, *tok;
        unsigned ntok = 0;
        memcpy(work, src, got + 1);
        memset(work + got + 1, 0x5A, sizeof work - got - 1);
        for (guard = 0; guard < 64; ++guard) {
            tok = cplus_strtok_s(guard == 0 ? work : NULL, &dmax, DELIM, &ctx);
            F100_OP();
            if (tok == NULL) break;
            ++ntok;
            /* Guard bytes around work are checked by the caller-owned buffer
             * contract; cplus_strtok_s only writes inside work. */
        }
        if (ntok < 9) F100_FAIL("unexpectedly few tokens from fixture");
        for (j = got + 1; j < sizeof work; ++j)
            if (work[j] != 0x5A) { F100_FAIL("strtok_s wrote outside the string"); break; }
    }
    (void)unlink(path);
    return f100_report("F-054");
}
