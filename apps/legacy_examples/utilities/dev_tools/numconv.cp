/* numconv.cp -- overflow-safe number base conversion (dec / hex / bin).
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/utilities/dev_tools/numconv.cp \
 *     --strict --sanitize=none -I. -o examples/utilities/dev_tools/numconv
 *
 * Usage:
 *   numconv <value> <from> <to>       from,to in {dec,hex,bin}
 *
 * Safety evidence (runtime refusal, not a formal proof):
 *   - a value that exceeds UINT32_MAX is REFUSED ("overflow") rather than
 *     wrapping; the guard uses shift-subtract division so strict mode never
 *     sees an unproven variable divisor;
 *   - a digit illegal for the source base is REFUSED ("invalid-digit");
 *   - an empty value, an unknown base name, or a bad argument count is REFUSED.
 *
 * Input is copied into a local bounded buffer before any per-character access,
 * so strict mode never dereferences an argv pointer directly.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "cplus/cplus.h"
#include "stdlib/string.hp"

#define NC_CAP 64

/* Exact unsigned division without a variable-divisor expression (strict safe). */
static unsigned int nc_udiv(unsigned int a, unsigned int b) {
    unsigned int q = 0;
    unsigned int r = a;
    for (int sh = 31; sh >= 0; --sh) {
        unsigned int d = 1u << sh;
        if (b <= (UINT_MAX >> sh)) {
            unsigned int v = b << sh;
            if (r >= v) { r -= v; q += d; }
        }
    }
    return q;
}

static int nc_base(const char *name, unsigned int *base) {
    if (strcmp(name, "dec") == 0) { *base = 10u; return 1; }
    if (strcmp(name, "hex") == 0) { *base = 16u; return 1; }
    if (strcmp(name, "bin") == 0) { *base = 2u;  return 1; }
    return 0;
}

/* Returns 1 on success, 0 on empty / invalid digit, -1 on overflow. */
static int nc_parse(const char *s, unsigned int base, unsigned int *out) {
    unsigned int v = 0;
    size_t i = 0;
    if (s[0] == '\0') return 0;
    for (i = 0; s[i] != '\0'; i++) {
        unsigned int d;
        char c = s[i];
        if (c >= '0' && c <= '9') d = (unsigned int)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned int)(c - 'a') + 10u;
        else if (c >= 'A' && c <= 'F') d = (unsigned int)(c - 'A') + 10u;
        else return 0;
        if (d >= base) return 0;
        /* v * base + d <= UINT_MAX  <=>  v <= (UINT_MAX - d) / base */
        if (v > nc_udiv(UINT_MAX - d, base)) return -1;
        v = v * base + d;
    }
    *out = v;
    return 1;
}

static void nc_to_bin(unsigned int v, char *out) {
    unsigned int mask = 1u << 31; /* 0x80000000: constant shift, width-safe */
    int started = 0;
    size_t k = 0;
    for (int i = 31; i >= 0; --i) {
        int bit = (v & mask) != 0u;
        if (bit || started || i == 0) {
            started = 1;
            out[k++] = bit ? '1' : '0';
        }
        mask = mask >> 1; /* constant single-bit step, no variable shift */
    }
    out[k] = '\0';
}

static char *nc_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static int nc_copy_arg(char **argv, size_t idx, char *buf, size_t cap) {
    string text = string(nc_arg(argv, idx));
    if (!text.valid()) { text.drop(); return 0; }
    if (text.size() + 1 > cap) { text.drop(); return 0; }
    if (text.substr(0, text.size(), buf, cap) != 0) { text.drop(); return 0; }
    text.drop();
    return 1;
}

int main(int argc, char **argv) {
    char value[NC_CAP];
    char from_s[16];
    char to_s[16];
    char result[64];
    unsigned int base_from = 0;
    unsigned int base_to = 0;
    unsigned int v = 0;
    int pr;

    if (argc != 4) {
        printf("NUMCONV REFUSED bad-args\n");
        return 1;
    }
    if (!nc_copy_arg(argv, 1, value, sizeof(value)) ||
        !nc_copy_arg(argv, 2, from_s, sizeof(from_s)) ||
        !nc_copy_arg(argv, 3, to_s, sizeof(to_s))) {
        printf("NUMCONV REFUSED input-too-long\n");
        return 1;
    }
    if (!nc_base(from_s, &base_from) || !nc_base(to_s, &base_to)) {
        printf("NUMCONV REFUSED unknown-base\n");
        return 1;
    }

    pr = nc_parse(value, base_from, &v);
    if (pr == 0) {
        printf("NUMCONV REFUSED invalid-digit\n");
        return 1;
    }
    if (pr < 0) {
        printf("NUMCONV REFUSED overflow\n");
        return 1;
    }

    if (base_to == 10u) snprintf(result, sizeof(result), "%u", v);
    else if (base_to == 16u) snprintf(result, sizeof(result), "%x", v);
    else nc_to_bin(v, result);

    printf("NUMCONV %s %s %s %s\n", value, from_s, to_s, result);
    return 0;
}
