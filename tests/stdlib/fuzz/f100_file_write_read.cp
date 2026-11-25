/* F-081 - deterministic fuzz config: file write_bytes / read with binary NUL.
 *
 * An anonymous temp file (tmpfile via file_temp) is driven with fixed-seed
 * bounded chunks that include 0x00 and high bytes.  A byte-exact shadow model
 * tracks the file contents and the current position; partial reads are checked
 * against it by comparing the bytes actually consumed (via ftell).  EOF is a
 * documented non-failure outcome.  Cleanup is repeated to prove idempotence.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

#define CASES 1200
#define MAXF 16384
#define CHUNK 8
#define NOPS 6

static unsigned long long rng_state = 0xF081C0DEULL;

static unsigned int rnd(void) {
    rng_state = rng_state * 6364136223846793005ULL +
                1442695040888963407ULL;
    return (unsigned int)(rng_state >> 33);
}

#define FNV_PRIME 1099511628211ULL

static unsigned long long mix(unsigned long long d, unsigned long long v) {
    return (d ^ v) * FNV_PRIME;
}

static unsigned char sh[MAXF];
static size_t total = 0;
static size_t pos = 0;

static int fail_here(const char *label) {
    print("F-081 INVARIANT", label);
    return 1;
}

int main(void) {
    unsigned long long digest = 1469598103934665603ULL;
    error err = error();
    file f = file_temp(&err);
    char buf[CHUNK];
    char text[CHUNK + 1];
    char hex[24];
    int i;
    int cases = 0;
    int rc;

    if (!f.is_open()) return fail_here("temp.open");

    for (i = 0; i < CASES; i++) {
        unsigned int r0 = rnd();
        unsigned int r1 = rnd();
        unsigned int r2 = rnd();
        int kind = (int)(r0 % (unsigned int)NOPS);
        size_t n = 1 + (size_t)(r1 % (unsigned int)CHUNK);
        size_t k;
        long tell;

        digest = mix(digest, (unsigned long long)i);
        digest = mix(digest, (unsigned long long)kind);
        switch (kind) {
        case 0: /* bounded binary write, including NUL and high bytes */
            if (total + n > MAXF)
                return fail_here("write.capacity");
            if (pos != total) {
                if (f.seek(0, SEEK_END, &err) != STDLIB_OK)
                    return fail_here("write.seek.end");
                pos = total;
            }
            for (k = 0; k < n; k++)
                buf[k] = (char)(unsigned char)((r2 >> (4u * (k % 4u))) & 0xFFu);
            if (f.write_bytes(buf, n, &err) != STDLIB_OK || !err.ok())
                return fail_here("write_bytes");
            for (k = 0; k < n; k++)
                sh[total + k] = (unsigned char)buf[k];
            total += n;
            pos = total;
            for (k = 0; k < n; k++)
                digest = mix(digest, (unsigned long long)(unsigned char)buf[k]);
            break;

        case 1: /* bounded NUL-terminated text write */
            if (total + n > MAXF)
                return fail_here("text.capacity");
            if (pos != total) {
                if (f.seek(0, SEEK_END, &err) != STDLIB_OK)
                    return fail_here("text.seek.end");
                pos = total;
            }
            for (k = 0; k < n; k++)
                text[k] = (char)('A' + (char)((r2 >> (4u * (k % 4u))) % 26u));
            text[n] = '\0';
            if (!f.write(text)) return fail_here("write.text");
            for (k = 0; k < n; k++)
                sh[total + k] = (unsigned char)text[k];
            total += n;
            pos = total;
            digest = mix(digest, (unsigned long long)n);
            break;

        case 2: /* partial read at a bounded, valid position */
            {
                size_t start = total > 0 ? (size_t)(r1 % total) : 0u;
                size_t want = 1 + (size_t)(r2 % (unsigned int)CHUNK);
                size_t avail = total - start;
                size_t got;
                if (f.seek((long)start, SEEK_SET, &err) != STDLIB_OK)
                    return fail_here("read.seek");
                pos = start;
                memset(buf, 0x7F, CHUNK);
                rc = f.read(buf, want, &err);
                if (avail == 0) {
                    if (rc != STDLIB_EOF || !err.ok())
                        return fail_here("read.eof");
                    got = 0;
                } else {
                    got = want < avail ? want : avail;
                    if (rc != STDLIB_OK || !err.ok())
                        return fail_here("read.status");
                    if (memcmp(buf, sh + start, got) != 0)
                        return fail_here("read.bytes");
                }
                pos = start + got;
                /* On this host a short fread at EOF can leave the C stdio
                 * position at the requested (rather than delivered) count.
                 * Resynchronise explicitly so the shadow stays authoritative. */
                if (got < want) {
                    if (f.seek((long)pos, SEEK_SET, &err) != STDLIB_OK)
                        return fail_here("read.resync");
                }
                tell = ftell(f.native());
                if (tell < 0 || (size_t)tell != pos)
                    return fail_here("read.tell");
                digest = mix(digest, (unsigned long long)got);
                digest = mix(digest, (unsigned long long)(rc + 2));
            }
            break;

        case 3: /* seek to start, then a short read from byte zero */
            if (f.seek(0, SEEK_SET, &err) != STDLIB_OK)
                return fail_here("start.seek");
            pos = 0;
            memset(buf, 0x5A, CHUNK);
            rc = f.read(buf, n, &err);
            {
                size_t got = total < n ? total : n;
                if (total == 0) {
                    if (rc != STDLIB_EOF || !err.ok())
                        return fail_here("start.eof");
                } else {
                    if (rc != STDLIB_OK || !err.ok() ||
                        memcmp(buf, sh, got) != 0)
                        return fail_here("start.bytes");
                }
                pos = got;
                if (got < n) {
                    if (f.seek((long)pos, SEEK_SET, &err) != STDLIB_OK)
                        return fail_here("start.resync");
                }
                digest = mix(digest, (unsigned long long)got);
            }
            break;

        case 4: /* flush must not perturb content or position */
            if (f.flush(&err) != STDLIB_OK || !err.ok())
                return fail_here("flush");
            tell = ftell(f.native());
            if (tell < 0 || (size_t)tell != pos)
                return fail_here("flush.tell");
            digest = mix(digest, (unsigned long long)pos);
            break;

        default: /* full replay from byte zero in small pieces */
            if (f.seek(0, SEEK_SET, &err) != STDLIB_OK)
                return fail_here("replay.seek");
            pos = 0;
            {
                size_t at = 0;
                while (at < total) {
                    size_t want = total - at < 4u ? total - at : 4u;
                    int r = f.read(buf, want, &err);
                    if (r != STDLIB_OK || !err.ok() ||
                        memcmp(buf, sh + at, want) != 0)
                        return fail_here("replay.bytes");
                    at += want;
                    digest = mix(digest, (unsigned long long)want);
                }
                rc = f.read(buf, 1, &err);
                if (rc != STDLIB_EOF || !err.ok())
                    return fail_here("replay.eof");
                pos = total;
                if (f.seek((long)pos, SEEK_SET, &err) != STDLIB_OK)
                    return fail_here("replay.resync");
            }
            break;
        }

        tell = ftell(f.native());
        if (tell < 0 || (size_t)tell != pos) {
            print("F-081 debug case", i, "kind", kind, "pos", (int)pos,
                  "tell", (int)tell, "total", (int)total);
            return fail_here("position.shadow");
        }
        if (ferror(f.native()) != 0)
            return fail_here("stream.error");
        digest = mix(digest, (unsigned long long)pos);
        digest = mix(digest, (unsigned long long)total);
        cases++;
    }

    /* Repeated cleanup: close is checked, drop is idempotent. */
    if (f.close(&err) != STDLIB_OK || !err.ok())
        return fail_here("close.first");
    if (f.close(&err) != STDLIB_FAIL || err.ok() ||
        err.category() != STDLIB_CATEGORY_LIFECYCLE)
        return fail_here("close.second");
    f.drop();
    f.drop();
    if (f.is_open() || f.native() != (FILE *)0)
        return fail_here("closed.state");

    snprintf(hex, sizeof(hex), "%016llx", digest);
    print("f100_file_write_read_digest", hex);
    print("f100_file_write_read:", cases, "cases ok");
    return 0;
}
