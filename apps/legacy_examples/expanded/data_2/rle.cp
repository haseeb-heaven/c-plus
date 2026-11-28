/* rle.cp -- deterministic bounded run-length encode/decode with roundtrip.
 *
 * NOT an interactive tool.  All input is fixed argv.  No clock, no network,
 * no user paths, no shelling out.  Identical argv yields byte-identical stdout.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/expanded/data_2/rle.cp \
 *       --strict --sanitize=none -I. -o apps/legacy_examples/expanded/data_2/.run/rle
 *
 * Usage:
 *   rle enc <raw>        raw bytes: printable ASCII except ';' (1..64)
 *   rle dec <encoded>    format: RLE1;<byte><nn>;<byte><nn>...
 *
 * <nn> is exactly two decimal digits (01..99).  Encoding is canonical.  Every
 * invocation performs a full internal roundtrip and prints `roundtrip ok=1`
 * only on byte-exact equality.  Oversized raw input, malformed streams and
 * decode output overflow are REFUSED (exit 2).
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

#define RL_MAX_RAW 64
#define RL_MAX_ENC 512
#define RL_MAGIC "RLE1;"
#define RL_MAGIC_LEN 5

static const char *rl_arg(char **argv, int i) {
    const char *v = NULL;
    if (i <= 0)
        return NULL;
    __builtin_memcpy(&v, argv + i, sizeof(v));
    return v;
}

static int rl_refuse(const char *app, const char *reason, cplus_exit_code code) {
    error e = error();
    e.set(code, reason);
    printf("%s error code=%d reason=%s\n", app, e.code(), reason);
    return 2;
}

static int rl_ok_byte(unsigned char c) {
    return c >= 0x21 && c <= 0x7e && c != ';';
}

static int rl_put(char *buf, size_t cap, size_t *pos, char c) {
    if (cap == 0 || *pos >= cap - 1)
        return -1;
    buf[*pos] = c;
    (*pos)++;
    return 0;
}

static int rl_puts(char *buf, size_t cap, size_t *pos, const char *text) {
    string s = string(text);
    size_t i;
    for (i = 0; i < s.size(); i++)
        if (rl_put(buf, cap, pos, s.at(i)) != 0)
            return -1;
    return 0;
}

/* -1 bad length, -2 internal buffer, -3 invalid byte, -4 run too long. */
static int rl_encode(const char *raw, char *out, size_t cap, int *groups) {
    string s = string(raw);
    size_t n = s.size();
    size_t i = 0;
    size_t pos = 0;
    int g = 0;
    if (n == 0 || n > RL_MAX_RAW)
        return -1;
    if (rl_puts(out, cap, &pos, RL_MAGIC) != 0)
        return -2;
    while (i < n) {
        char b = s.at(i);
        size_t run = 1;
        if (!rl_ok_byte((unsigned char)b))
            return -3;
        while (i + run < n && s.at(i + run) == b)
            run++;
        if (run > 99)
            return -4;
        if (g > 0 && rl_put(out, cap, &pos, ';') != 0)
            return -2;
        if (rl_put(out, cap, &pos, b) != 0)
            return -2;
        if (rl_put(out, cap, &pos, (char)('0' + (int)(run / 10))) != 0)
            return -2;
        if (rl_put(out, cap, &pos, (char)('0' + (int)(run % 10))) != 0)
            return -2;
        g++;
        i += run;
    }
    out[pos] = '\0';
    *groups = g;
    return 0;
}

/* -1 bad magic/short, -2 malformed, -3 output overflow, -4 alloc,
 * -5 invalid byte.  Decoded length is bounded by out_cap-1 at every group. */
static int rl_decode(const char *encoded, char *out, size_t out_cap,
                     size_t *out_len, vector *bytes, vector *runs) {
    string s = string(encoded);
    string magic = string(RL_MAGIC);
    size_t n = s.size();
    size_t i = 0;
    size_t pos = 0;
    size_t k;
    if (out_cap == 0)
        return -3;
    if (n < RL_MAGIC_LEN + 3)
        return -1;
    for (k = 0; k < RL_MAGIC_LEN; k++)
        if (s.at(k) != magic.at(k))
            return -1;
    i = RL_MAGIC_LEN;
    while (i < n) {
        char b = s.at(i);
        int d1;
        int d2;
        int run;
        if (!rl_ok_byte((unsigned char)b))
            return -5;
        if (i + 2 >= n)
            return -2;
        d1 = (int)(unsigned char)s.at(i + 1) - '0';
        d2 = (int)(unsigned char)s.at(i + 2) - '0';
        if (d1 < 0 || d1 > 9 || d2 < 0 || d2 > 9)
            return -2;
        run = d1 * 10 + d2;
        if (run < 1 || run > 99)
            return -2;
        if (pos + (size_t)run > out_cap - 1)
            return -3;
        for (k = 0; k < (size_t)run; k++)
            out[pos + k] = b;
        pos += (size_t)run;
        if (bytes->push_back((int)(unsigned char)b) != 0 ||
            runs->push_back(run) != 0)
            return -4;
        i += 3;
        if (i < n) {
            if (s.at(i) != ';')
                return -2;
            i++;
            if (i >= n)
                return -2;
        }
    }
    if (pos == 0)
        return -2;
    out[pos] = '\0';
    *out_len = pos;
    return 0;
}

static int rl_enc_refusal(int r) {
    if (r == -1)
        return rl_refuse("RLE", "raw-too-long", CPLUS_EXIT_BOUNDS);
    if (r == -3)
        return rl_refuse("RLE", "invalid-byte", CPLUS_EXIT_UNSUPPORTED);
    if (r == -4)
        return rl_refuse("RLE", "run-too-long", CPLUS_EXIT_BOUNDS);
    return rl_refuse("RLE", "encode-buffer-overflow", CPLUS_EXIT_BOUNDS);
}

static int rl_dec_refusal(int r) {
    if (r == -1)
        return rl_refuse("RLE", "bad-magic", CPLUS_EXIT_UNSUPPORTED);
    if (r == -2)
        return rl_refuse("RLE", "malformed-stream", CPLUS_EXIT_UNSUPPORTED);
    if (r == -3)
        return rl_refuse("RLE", "decoded-overflow", CPLUS_EXIT_BOUNDS);
    if (r == -4)
        return rl_refuse("RLE", "alloc-failed", CPLUS_EXIT_RESOURCE);
    return rl_refuse("RLE", "invalid-byte", CPLUS_EXIT_UNSUPPORTED);
}

static void rl_print_groups(vector *bytes, vector *runs) {
    size_t i;
    for (i = 0; i < runs->size(); i++)
        printf("RLE group %d byte=%c run=%d\n", (int)i,
               (char)bytes->at(i), runs->at(i));
}

int main(int argc, char **argv) {
    const char *op;
    const char *payload;
    int is_enc;
    char encbuf[RL_MAX_ENC];
    char rawbuf[RL_MAX_RAW + 1];
    char again[RL_MAX_ENC];
    vector bytes = vector();
    vector runs = vector();
    size_t decoded_len = 0;
    int groups = 0;
    int r;

    if (argc != 3)
        return rl_refuse("RLE", "usage", CPLUS_EXIT_UNSUPPORTED);
    op = rl_arg(argv, 1);
    payload = rl_arg(argv, 2);
    if (op == NULL || payload == NULL)
        return rl_refuse("RLE", "null-arg", CPLUS_EXIT_NULL_POINTER);
    if (strcmp(op, "enc") == 0)
        is_enc = 1;
    else if (strcmp(op, "dec") == 0)
        is_enc = 0;
    else
        return rl_refuse("RLE", "bad-op", CPLUS_EXIT_UNSUPPORTED);

    printf("RLE app=rle op=%s max_raw=%d max_run=99 magic=%s\n",
           is_enc ? "enc" : "dec", RL_MAX_RAW, RL_MAGIC);

    if (is_enc) {
        string raw = string(payload);
        printf("RLE in_len=%d in=%s\n", (int)raw.size(), payload);
        r = rl_encode(payload, encbuf, RL_MAX_ENC, &groups);
        if (r != 0)
            return rl_enc_refusal(r);
        r = rl_decode(encbuf, rawbuf, RL_MAX_RAW + 1, &decoded_len,
                      &bytes, &runs);
        if (r != 0)
            return rl_dec_refusal(r);
        rl_print_groups(&bytes, &runs);
        printf("RLE encoded=%s groups=%d enc_len=%d\n", encbuf, groups,
               (int)strlen(encbuf));
        if (decoded_len != raw.size() ||
            strcmp(rawbuf, payload) != 0)
            return rl_refuse("RLE", "roundtrip-failed",
                             CPLUS_EXIT_UNSUPPORTED);
        printf("RLE roundtrip ok=1 decoded_len=%d\n", (int)decoded_len);
    } else {
        printf("RLE enc=%s\n", payload);
        r = rl_decode(payload, rawbuf, RL_MAX_RAW + 1, &decoded_len,
                      &bytes, &runs);
        if (r != 0)
            return rl_dec_refusal(r);
        rl_print_groups(&bytes, &runs);
        printf("RLE decoded_len=%d decoded=%s\n", (int)decoded_len, rawbuf);
        r = rl_encode(rawbuf, again, RL_MAX_ENC, &groups);
        if (r != 0)
            return rl_enc_refusal(r);
        if (strcmp(again, payload) != 0)
            return rl_refuse("RLE", "roundtrip-failed",
                             CPLUS_EXIT_UNSUPPORTED);
        printf("RLE roundtrip ok=1 groups=%d\n", groups);
    }
    printf("RLE done\n");

    bytes.drop();
    runs.drop();
    return 0;
}
