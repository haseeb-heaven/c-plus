/* F-060: combined format + I/O roundtrip in two phases.
 * Phase A writes 400 "%d:%llx:%.3f" text records with cplus_fprintf_s, rewinds,
 * and parses them back with cplus_fscanf_s (doubles are multiples of 0.25 so
 * the decimal round-trip is exact).
 * Phase B writes/reads the same pre-generated records as bounded binary bytes
 * with cplus_fwrite_s/cplus_fread_s in a separate temp file.  Records are
 * generated once into an array so both directions consume the identical LCG
 * values (re-generating in the read loop would advance the stream state). */
#include "f100_io_common.h"

typedef struct { int a; unsigned long long u; double d; } record;

#define NREC 400

static void gen_record(record *r) {
    r->a = (int)(f100_next() % 20000UL) - 10000;
    r->u = ((unsigned long long)f100_next() << 32) ^ f100_next();
    r->d = (double)(f100_next() % 4000UL) / 4.0;   /* exact multiples of 0.25 */
}

int main(void) {
    char path[512];
    FILE *f;
    record recs[NREC], got;
    unsigned char wbin[96], rbin[96];
    unsigned i;
    size_t nbin;
    int rc;

    f100_init();
    for (i = 0; i < NREC; ++i) gen_record(&recs[i]);

    f100_tmp_path(path, sizeof path, "roundtrip");
    f = cplus_fopen_s(path, "wb+");
    if (f == NULL) { F100_FAIL("fopen_s failed (text)"); return f100_report("F-060"); }
    for (i = 0; i < NREC; ++i) {
        rc = cplus_fprintf_s(f, "%d:%llx:%.3f\n", recs[i].a, recs[i].u, recs[i].d);
        F100_OP();
        if (rc <= 0) F100_FAIL("fprintf_s record failed");
    }
    fflush(f);
    rewind(f);
    for (i = 0; i < NREC; ++i) {
        got.a = -1; got.u = 0; got.d = 0.0;
        rc = cplus_fscanf_s(f, "%d:%llx:%lf", &got.a, &got.u, &got.d);
        F100_OP();
        if (rc != 3 || got.a != recs[i].a || got.u != recs[i].u || got.d != recs[i].d)
            F100_FAIL("fscanf_s record mismatch");
    }
    if (cplus_fclose_s(f) != 0) F100_FAIL("text close failed");
    (void)unlink(path);

    f100_tmp_path(path, sizeof path, "roundtripbin");
    f = cplus_fopen_s(path, "wb+");
    if (f == NULL) { F100_FAIL("fopen_s failed (binary)"); return f100_report("F-060"); }
    for (i = 0; i < NREC; ++i) {
        nbin = (size_t)snprintf((char *)wbin, sizeof wbin, "%d/%llx/%.3f",
                                recs[i].a, recs[i].u, recs[i].d);
        if (cplus_fwrite_s(wbin, sizeof wbin, 1, nbin, f) != nbin)
            F100_FAIL("fwrite_s binary record failed");
        F100_OP();
    }
    fflush(f);
    rewind(f);
    for (i = 0; i < NREC; ++i) {
        nbin = (size_t)snprintf((char *)wbin, sizeof wbin, "%d/%llx/%.3f",
                                recs[i].a, recs[i].u, recs[i].d);
        memset(rbin, 0, sizeof rbin);
        if (cplus_fread_s(rbin, sizeof rbin, 1, nbin, f) != nbin)
            F100_FAIL("fread_s binary record failed");
        F100_OP();
        if (memcmp(wbin, rbin, nbin) != 0) F100_FAIL("binary record mismatch");
    }
    if (cplus_fclose_s(f) != 0) F100_FAIL("binary close failed");
    (void)unlink(path);
    return f100_report("F-060");
}
