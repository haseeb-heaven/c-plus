/* hash_tool.cp -- deterministic bounded FNV-1a/32 hasher.
 *
 * NOT an interactive tool.  All input is fixed argv.  No clock, no network,
 * no user paths, no shelling out.  Identical argv yields byte-identical stdout.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/data_2/hash_tool.cp \
 *       --strict --sanitize=none -I. -o examples/expanded/data_2/.run/hash_tool
 *
 * Usage:
 *   hash_tool <text> [<text> ...]
 *
 * Each token is hashed independently with FNV-1a (32-bit).  Length is capped
 * at 64 bytes and the number of tokens at 16.  Oversized, empty and
 * non-printable inputs are REFUSED (exit 2).  A built-in known-answer check
 * (FNV-1a("hello") == 0x4f9f2cab) must pass before any input is processed.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

#define HT_MAX_INPUTS 16
#define HT_MAX_LEN 64
#define HT_OFFSET 0x811c9dc5u
#define HT_PRIME 0x01000193u
#define HT_SELF_TEXT "hello"
#define HT_SELF_HASH 0x4f9f2cabu

static const char *ht_arg(char **argv, int i) {
    const char *v = NULL;
    if (i <= 0)
        return NULL;
    __builtin_memcpy(&v, argv + i, sizeof(v));
    return v;
}

static int ht_refuse(const char *app, const char *reason, cplus_exit_code code) {
    error e = error();
    e.set(code, reason);
    printf("%s error code=%d reason=%s\n", app, e.code(), reason);
    return 2;
}

static unsigned int ht_fnv1a(const char *text) {
    string s = string(text);
    unsigned int h = HT_OFFSET;
    size_t i;
    size_t n = s.size();
    for (i = 0; i < n; i++) {
        h ^= (unsigned int)(unsigned char)s.at(i);
        h *= HT_PRIME;
    }
    return h;
}

/* -1 empty, -2 too long, -3 non-printable byte, 0 on success. */
static int ht_validate(const char *text, size_t *out_len) {
    string s = string(text);
    size_t n = s.size();
    size_t i;
    if (n == 0)
        return -1;
    if (n > HT_MAX_LEN)
        return -2;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s.at(i);
        if (c < 0x21 || c > 0x7e)
            return -3;
    }
    *out_len = n;
    return 0;
}

int main(int argc, char **argv) {
    vector lengths = vector();
    unsigned int xor_all = 0u;
    unsigned int self_value;
    int n;
    int i;

    self_value = ht_fnv1a(HT_SELF_TEXT);
    if (self_value != HT_SELF_HASH)
        return ht_refuse("HASH", "selfcheck-failed", CPLUS_EXIT_UNSUPPORTED);

    if (argc < 2)
        return ht_refuse("HASH", "usage", CPLUS_EXIT_UNSUPPORTED);
    n = argc - 1;
    if (n > HT_MAX_INPUTS)
        return ht_refuse("HASH", "too-many-inputs", CPLUS_EXIT_BOUNDS);

    /* Validate every token before printing the header: no partial transcript
     * is ever emitted for a refused invocation. */
    for (i = 0; i < n; i++) {
        const char *arg = ht_arg(argv, i + 1);
        size_t len = 0;
        int r;
        if (arg == NULL)
            return ht_refuse("HASH", "null-input", CPLUS_EXIT_NULL_POINTER);
        r = ht_validate(arg, &len);
        if (r == -1)
            return ht_refuse("HASH", "empty-input", CPLUS_EXIT_UNSUPPORTED);
        if (r == -2)
            return ht_refuse("HASH", "input-too-long", CPLUS_EXIT_BOUNDS);
        if (r == -3)
            return ht_refuse("HASH", "invalid-byte", CPLUS_EXIT_UNSUPPORTED);
        if (lengths.push_back((int)len) != 0)
            return ht_refuse("HASH", "alloc-failed", CPLUS_EXIT_RESOURCE);
    }

    printf("HASH app=hash_tool alg=FNV-1a-32 offset=0x%08x prime=0x%08x "
           "max_len=%d selfcheck=ok n=%d\n",
           HT_OFFSET, HT_PRIME, HT_MAX_LEN, n);
    for (i = 0; i < n; i++) {
        const char *arg = ht_arg(argv, i + 1);
        unsigned int value = ht_fnv1a(arg);
        int len = lengths.at((size_t)i);
        printf("HASH %d len=%d hash=0x%08x text=%s\n", i, len, value, arg);
        xor_all ^= value;
    }
    printf("HASH xor=0x%08x\n", xor_all);
    printf("HASH done\n");

    lengths.drop();
    return 0;
}
