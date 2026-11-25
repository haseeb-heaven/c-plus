/* sha_lite.cp -- deterministic 32-bit FNV-1a hash of a bounded argv string.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/utilities/dev_tools/sha_lite.cp \
 *     --strict --sanitize=none -I. -o apps/legacy_examples/utilities/dev_tools/sha_lite
 *
 * Usage:
 *   sha_lite <input-string>
 *
 * Safety evidence (runtime refusal, not a formal proof):
 *   - an input longer than 64 bytes is REFUSED ("input-too-long") instead of
 *     overflowing the fixed local buffer;
 *   - a missing/extra argument is REFUSED ("bad-args").
 *
 * FNV-1a is a fixed public algorithm (offset basis 0x811c9dc5, prime
 * 0x01000193) so the emitted hash values are reproducible on any platform.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/string.hp"

#define SL_MAX 64

static unsigned int sl_hash(const char *s) {
    unsigned int h = 2166136261u; /* 0x811c9dc5 */
    size_t i = 0;
    for (i = 0; s[i] != '\0'; i++) {
        h = h ^ (unsigned int)(unsigned char)s[i];
        h = h * 16777619u; /* 0x01000193 */
    }
    return h;
}

static char *sl_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

/* Copy argv into `buf`; returns the byte length, or -1 if too long. */
static long sl_copy(char **argv, size_t idx, char *buf, size_t cap) {
    string text = string(sl_arg(argv, idx));
    long n;
    if (!text.valid()) { text.drop(); return -1; }
    if (text.size() + 1 > cap) { text.drop(); return -1; }
    if (text.substr(0, text.size(), buf, cap) != 0) { text.drop(); return -1; }
    n = (long)text.size();
    text.drop();
    return n;
}

int main(int argc, char **argv) {
    char buf[SL_MAX + 1];
    long len;

    if (argc != 2) {
        printf("SHA_LITE REFUSED bad-args\n");
        return 1;
    }
    len = sl_copy(argv, 1, buf, sizeof(buf));
    if (len < 0) {
        printf("SHA_LITE REFUSED input-too-long\n");
        return 1;
    }

    printf("SHA_LITE %ld %08x\n", len, sl_hash(buf));
    return 0;
}
