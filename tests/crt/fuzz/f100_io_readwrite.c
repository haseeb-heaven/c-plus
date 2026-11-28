/* F-052: bounded fwrite_s/fread_s round-trip over an LCG-selected byte
 * pattern and count (1..256), with bmax clamping and invalid-argument probes.
 * The read-back bytes are compared to what was written. */
#include "f100_io_common.h"

int main(void) {
    char path[512];
    unsigned char w[512], r[512];
    size_t count, i;
    FILE *f;

    f100_init();
    f100_tmp_path(path, sizeof path, "readwrite");
    f = cplus_fopen_s(path, "wb+");
    if (f == NULL) { F100_FAIL("fopen_s failed"); return f100_report("F-052"); }
    for (i = 0; i < 1200; ++i) {
        count = 1u + f100_below(256u);
        {
            size_t k;
            for (k = 0; k < count; ++k) w[k] = (unsigned char)(f100_next() >> 24);
            memset(w + count, 0, sizeof w - count);
        }
        memset(r, 0, sizeof r);
        rewind(f);
        if (cplus_fwrite_s(w, sizeof w, 1, count, f) != count)
            F100_FAIL("fwrite_s short write");
        F100_OP();
        rewind(f);
        if (cplus_fread_s(r, sizeof r, 1, count, f) != count)
            F100_FAIL("fread_s short read");
        F100_OP();
        if (memcmp(w, r, count) != 0) F100_FAIL("read-back mismatch");
        /* bmax clamp: asking for more than the buffer must not overflow. */
        if (cplus_fread_s(r, 8u, 1, 200u, f) > 8u)
            F100_FAIL("fread_s ignored bmax clamp");
        F100_OP();
    }
    if (cplus_fread_s(NULL, sizeof r, 1, 1, f) != 0)
        F100_FAIL("NULL read buffer accepted");
    F100_OP();
    if (cplus_fwrite_s(w, sizeof w, 0, 1, f) != 0)
        F100_FAIL("zero element size accepted");
    F100_OP();
    if (cplus_fclose_s(f) != 0) F100_FAIL("final close failed");
    (void)unlink(path);
    return f100_report("F-052");
}
