/* envfilter.cp -- filter KEY=VALUE style lines by a key prefix.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/utilities/dev_tools/envfilter.cp \
 *     --strict --sanitize=none -I. -o examples/utilities/dev_tools/envfilter
 *
 * Usage:
 *   envfilter <prefix> <KEY=VALUE> [KEY=VALUE ...]
 *
 * Lines come from argv only; the real process environment is never read, so
 * the tool is deterministic.  Safety evidence (runtime refusal, not a proof):
 *   - a line with no '=', an empty key, or an illegal key character is REFUSED
 *     ("malformed" / "empty-key" / "bad-key");
 *   - a line longer than the fixed 128-byte buffer is REFUSED ("oversized");
 *   - an invalid prefix is REFUSED ("bad-prefix").
 *
 * All argv strings are copied into local bounded buffers before inspection.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/string.hp"

#define EF_LINE_CAP 128
#define EF_PREFIX_CAP 32

static char *ef_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static int ef_copy(char **argv, size_t idx, char *buf, size_t cap) {
    string text = string(ef_arg(argv, idx));
    if (!text.valid()) { text.drop(); return 0; }
    if (text.size() + 1 > cap) { text.drop(); return 0; }
    if (text.substr(0, text.size(), buf, cap) != 0) { text.drop(); return 0; }
    text.drop();
    return 1;
}

static int ef_is_key_char(char c, int first) {
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= 'a' && c <= 'z') return 1;
    if (c == '_') return 1;
    if (!first && c >= '0' && c <= '9') return 1;
    return 0;
}

int main(int argc, char **argv) {
    char prefix[EF_PREFIX_CAP + 1];
    char line[EF_LINE_CAP + 1];
    size_t prefix_len = 0;
    size_t i;
    int argi;
    int matched = 0;

    if (argc < 2) {
        printf("ENVFILTER REFUSED bad-args\n");
        return 1;
    }
    if (!ef_copy(argv, 1, prefix, sizeof(prefix))) {
        printf("ENVFILTER REFUSED bad-prefix\n");
        return 1;
    }
    prefix_len = strlen(prefix);
    if (prefix_len == 0) {
        printf("ENVFILTER REFUSED bad-prefix\n");
        return 1;
    }
    for (i = 0; i < prefix_len; i++) {
        if (!ef_is_key_char(prefix[i], i == 0)) {
            printf("ENVFILTER REFUSED bad-prefix\n");
            return 1;
        }
    }

    if (argc == 2) {
        printf("ENVFILTER REFUSED bad-args\n");
        return 1;
    }

    for (argi = 2; argi < argc; argi++) {
        char *eq;
        size_t key_len;
        size_t k;
        int starts_with_prefix = 0;

        if (!ef_copy(argv, (size_t)argi, line, sizeof(line))) {
            printf("ENVFILTER REFUSED oversized\n");
            return 1;
        }
        eq = strchr(line, '=');
        if (eq == NULL) {
            printf("ENVFILTER REFUSED malformed\n");
            return 1;
        }
        key_len = (size_t)(eq - line);
        if (key_len == 0) {
            printf("ENVFILTER REFUSED empty-key\n");
            return 1;
        }
        for (k = 0; k < key_len; k++) {
            if (!ef_is_key_char(line[k], k == 0)) {
                printf("ENVFILTER REFUSED bad-key\n");
                return 1;
            }
        }
        if (prefix_len <= key_len && memcmp(line, prefix, prefix_len) == 0)
            starts_with_prefix = 1;

        if (starts_with_prefix) {
            printf("ENVFILTER match %s\n", line);
            matched++;
        }
    }

    printf("ENVFILTER %s %d\n", prefix, matched);
    return 0;
}
