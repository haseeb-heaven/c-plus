/* F-082 - deterministic fuzz config: file read_line replay.
 *
 * A fixed-seed corpus of 32 ASCII lines (empty, exact-fit, CRLF and overlong)
 * is written once to an anonymous temp file.  1200 read_line operations replay
 * the corpus against an independent line model: status, error category and
 * returned text are checked on every call.  EOF and repeated cleanup are
 * verified at the end.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

#define CASES 1200
#define NLINES 32
#define LBUF 8
#define RAWCAP 1024

static unsigned long long rng_state = 0xF082C0DEULL;

static unsigned int rnd(void) {
    rng_state = rng_state * 6364136223846793005ULL +
                1442695040888963407ULL;
    return (unsigned int)(rng_state >> 33);
}

#define FNV_PRIME 1099511628211ULL

static unsigned long long mix(unsigned long long d, unsigned long long v) {
    return (d ^ v) * FNV_PRIME;
}

static char lines[NLINES][16];
static size_t llen[NLINES];
static int lover[NLINES];
static unsigned char raw[RAWCAP];
static size_t rawn = 0;

static int fail_here(const char *label) {
    print("F-082 INVARIANT", label);
    return 1;
}

int main(void) {
    unsigned long long digest = 1469598103934665603ULL;
    static const size_t lens[8] = { 0, 1, 3, 5, 7, 8, 9, 12 };
    error err = error();
    file f = file_temp(&err);
    char buf[LBUF];
    char hex[24];
    int i;
    int cases = 0;
    int li = 0;
    int rc;
    size_t j, k;

    if (!f.is_open()) return fail_here("temp.open");

    /* Build the deterministic line corpus and its byte image. */
    for (j = 0; j < NLINES; j++) {
        size_t L = lens[j % 8u];
        unsigned int seed = rnd();
        llen[j] = L;
        lover[j] = L >= (size_t)LBUF;
        for (k = 0; k < L; k++) {
            char c = (char)('a' + (char)((seed >> (4u * (k % 4u))) % 26u));
            lines[j][k] = c;
            raw[rawn++] = (unsigned char)c;
        }
        lines[j][L] = '\0';
        if (j % 2u == 0) {
            raw[rawn++] = (unsigned char)'\n';
        } else {
            raw[rawn++] = (unsigned char)'\r';
            raw[rawn++] = (unsigned char)'\n';
        }
    }
    if (f.write_bytes((const char *)raw, rawn, &err) != STDLIB_OK ||
        !err.ok())
        return fail_here("corpus.write");
    if (f.seek(0, SEEK_SET, &err) != STDLIB_OK)
        return fail_here("corpus.rewind");

    for (i = 0; i < CASES; i++) {
        if (li == NLINES) {
            if (f.seek(0, SEEK_SET, &err) != STDLIB_OK)
                return fail_here("replay.rewind");
            li = 0;
        }
        j = (size_t)li;
        memset(buf, 'Z', LBUF);
        rc = f.read_line(buf, LBUF, &err);

        digest = mix(digest, (unsigned long long)i);
        digest = mix(digest, (unsigned long long)j);
        if (lover[j]) {
            if (rc != STDLIB_FAIL || err.ok() ||
                err.category() != STDLIB_CATEGORY_BOUNDS ||
                err.code() != (int)CPLUS_EXIT_BOUNDS || buf[0] != '\0') {
                print("F-082 INVARIANT overlong", (int)j, "rc", rc,
                      "cat", err.category(), "code", err.code());
                return 1;
            }
            digest = mix(digest, (unsigned long long)llen[j]);
        } else {
            if (rc != STDLIB_OK || !err.ok() || strcmp(buf, lines[j]) != 0) {
                print("F-082 INVARIANT line", (int)j, "rc", rc,
                      "want", lines[j], "got", buf);
                return 1;
            }
            digest = mix(digest, (unsigned long long)llen[j]);
            for (k = 0; k < llen[j]; k++)
                digest = mix(digest, (unsigned long long)(unsigned char)buf[k]);
        }
        digest = mix(digest, (unsigned long long)(rc + 2));
        li++;
        cases++;
    }

    /* Seek to physical EOF (1200 is not a multiple of NLINES), then confirm a
     * documented EOF with an untouched clear error value. */
    if (f.seek(0, SEEK_END, &err) != STDLIB_OK)
        return fail_here("final.seek");
    memset(buf, 'Z', LBUF);
    rc = f.read_line(buf, LBUF, &err);
    if (rc != STDLIB_EOF || !err.ok() || buf[0] != 'Z')
        return fail_here("final.eof");

    if (f.close(&err) != STDLIB_OK || !err.ok())
        return fail_here("close.first");
    if (f.close(&err) != STDLIB_FAIL || err.ok() ||
        err.category() != STDLIB_CATEGORY_LIFECYCLE)
        return fail_here("close.second");
    f.drop();
    f.drop();

    snprintf(hex, sizeof(hex), "%016llx", digest);
    print("f100_file_lines_digest", hex);
    print("f100_file_lines:", cases, "cases ok");
    return 0;
}
