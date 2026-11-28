/* F-083 - deterministic fuzz config: file open/close/borrow/cleanup lifecycle.
 *
 * Uses process-unique but deterministic named temp files (no user paths) plus
 * anonymous file_temp handles.  Every update exercises open, write, flush,
 * seek, read, checked close, repeated close/drop and file removal; one variant
 * keeps two independent handles alive at once.  Invalid paths/modes stay
 * recoverable.  No path or pointer value is folded into the digest.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

#define CASES 1200
#define NOPS 5

static unsigned long long rng_state = 0xF083C0DEULL;

static unsigned int rnd(void) {
    rng_state = rng_state * 6364136223846793005ULL +
                1442695040888963407ULL;
    return (unsigned int)(rng_state >> 33);
}

#define FNV_PRIME 1099511628211ULL

static unsigned long long mix(unsigned long long d, unsigned long long v) {
    return (d ^ v) * FNV_PRIME;
}

static int path_absent(const char *path) {
    FILE *probe = fopen(path, "r");
    if (probe == NULL)
        return 1;
    fclose(probe);
    return 0;
}

static int fail_here(const char *label) {
    print("F-083 INVARIANT", label);
    return 1;
}

int main(void) {
    unsigned long long digest = 1469598103934665603ULL;
    error err = error();
    char path[96];
    char badpath[112];
    char buf[8];
    char hex[24];
    int i;
    int cases = 0;
    int rc;

    snprintf(path, sizeof(path), "cplus_f100_file_%d.tmp", (int)getpid());
    snprintf(badpath, sizeof(badpath),
             "cplus_f100_missing_%d/no_such_file.tmp", (int)getpid());
    if (!path_absent(path)) {
        if (remove(path) != 0) return fail_here("pre.cleanup");
    }

    for (i = 0; i < CASES; i++) {
        unsigned int r0 = rnd();
        unsigned int r1 = rnd();
        int kind = (int)(r0 % (unsigned int)NOPS);
        char payload[4];
        size_t k;

        digest = mix(digest, (unsigned long long)i);
        digest = mix(digest, (unsigned long long)kind);
        for (k = 0; k < 4; k++) {
            payload[k] = (char)(unsigned char)((r1 >> (4u * k)) & 0xFFu);
            digest = mix(digest, (unsigned long long)(unsigned char)payload[k]);
        }

        switch (kind) {
        case 0: /* named temp: open, write, close, remove */
            {
                file nf = file(path, "w+b");
                if (!nf.is_open() || nf.native() == (FILE *)0)
                    return fail_here("named.open");
                if (nf.write_bytes(payload, 4, &err) != STDLIB_OK || !err.ok())
                    return fail_here("named.write");
                if (nf.close(&err) != STDLIB_OK || !err.ok())
                    return fail_here("named.close");
                if (nf.close(&err) != STDLIB_FAIL || err.ok() ||
                    err.category() != STDLIB_CATEGORY_LIFECYCLE)
                    return fail_here("named.reclose");
                nf.drop(); nf.drop();
                if (nf.is_open() || nf.native() != (FILE *)0)
                    return fail_here("named.closed");
                if (remove(path) != 0 || !path_absent(path))
                    return fail_here("named.remove");
                digest = mix(digest, 1ULL);
            }
            break;

        case 1: /* named temp: flush, seek, partial read, repeated cleanup */
            {
                file nf = file(path, "w+b");
                if (!nf.is_open()) return fail_here("rw.open");
                if (nf.write_bytes(payload, 4, &err) != STDLIB_OK ||
                    nf.flush(&err) != STDLIB_OK || !err.ok())
                    return fail_here("rw.write.flush");
                if (nf.seek(0, SEEK_SET, &err) != STDLIB_OK)
                    return fail_here("rw.seek");
                memset(buf, 0x11, sizeof(buf));
                rc = nf.read(buf, 4, &err);
                if (rc != STDLIB_OK || !err.ok() ||
                    memcmp(buf, payload, 4) != 0)
                    return fail_here("rw.read");
                if (nf.close(&err) != STDLIB_OK || !err.ok())
                    return fail_here("rw.close");
                nf.drop();
                if (remove(path) != 0 || !path_absent(path))
                    return fail_here("rw.remove");
                digest = mix(digest, (unsigned long long)(rc + 2));
            }
            break;

        case 2: /* anonymous temp: write + repeated cleanup */
            {
                file af = file_temp(&err);
                if (!af.is_open()) return fail_here("temp.open");
                if (af.write_bytes(payload, 4, &err) != STDLIB_OK || !err.ok())
                    return fail_here("temp.write");
                if (af.close(&err) != STDLIB_OK || !err.ok())
                    return fail_here("temp.close");
                if (af.close(&err) != STDLIB_FAIL || err.ok())
                    return fail_here("temp.reclose");
                af.drop(); af.drop();
                digest = mix(digest, 2ULL);
            }
            break;

        case 3: /* two independent anonymous handles, interleaved cleanup */
            {
                file x = file_temp(&err);
                file y = file_temp(&err);
                if (!x.is_open() || !y.is_open())
                    return fail_here("multi.open");
                if (x.write_bytes(payload, 4, &err) != STDLIB_OK ||
                    y.write_bytes("yz", 2, &err) != STDLIB_OK || !err.ok())
                    return fail_here("multi.write");
                if (x.seek(0, SEEK_SET, &err) != STDLIB_OK ||
                    y.seek(0, SEEK_SET, &err) != STDLIB_OK)
                    return fail_here("multi.seek");
                memset(buf, 0x22, sizeof(buf));
                rc = x.read(buf, 4, &err);
                if (rc != STDLIB_OK || !err.ok() ||
                    memcmp(buf, payload, 4) != 0)
                    return fail_here("multi.read.x");
                rc = y.read(buf, 2, &err);
                if (rc != STDLIB_OK || !err.ok() || buf[0] != 'y' ||
                    buf[1] != 'z')
                    return fail_here("multi.read.y");
                if (x.close(&err) != STDLIB_OK || y.close(&err) != STDLIB_OK)
                    return fail_here("multi.close");
                if (x.close(&err) != STDLIB_FAIL || y.close(&err) != STDLIB_FAIL)
                    return fail_here("multi.reclose");
                x.drop(); y.drop(); x.drop(); y.drop();
                digest = mix(digest, 3ULL);
            }
            break;

        default: /* invalid path / mode are recoverable */
            {
                file bad = file(badpath, "r");
                if (bad.is_open() || bad.native() != (FILE *)0)
                    return fail_here("invalid.open.path");
                memset(buf, 0x33, sizeof(buf));
                rc = bad.read(buf, 4, &err);
                if (rc != STDLIB_FAIL || err.ok() ||
                    err.category() != STDLIB_CATEGORY_IO)
                    return fail_here("invalid.read");
                bad.drop(); bad.drop();
                {
                    file badmode = file(path, "q");
                    if (badmode.is_open() || badmode.native() != (FILE *)0)
                        return fail_here("invalid.open.mode");
                    badmode.drop();
                }
                digest = mix(digest, (unsigned long long)(rc + 2));
            }
            break;
        }

        if (r0 % 97u == 0u && !path_absent(path)) {
            /* Defensive cleanup; normal cases always remove their own file. */
            if (remove(path) != 0) return fail_here("defensive.remove");
        }
        cases++;
    }

    if (!path_absent(path))
        return fail_here("final.cleanup");

    snprintf(hex, sizeof(hex), "%016llx", digest);
    print("f100_file_lifecycle_digest", hex);
    print("f100_file_lifecycle:", cases, "cases ok");
    return 0;
}
