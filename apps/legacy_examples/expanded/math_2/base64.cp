/* base64.cp -- RFC 4648 base64 with bounded input and byte-exact roundtrip.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/expanded/math_2/base64.cp --strict --sanitize=none -I. \
 *       -o apps/legacy_examples/expanded/math_2/.run/base64
 *
 * Usage:
 *   base64                    built-in RFC vectors + decode/roundtrip
 *   base64 enc <ascii>        encode one fixed ASCII argument
 *   base64 dec <encoded>      decode one fixed base64 argument -> len + hex
 *   base64 oversized          plain input of B64_CAP+1 -> REFUSED
 *   base64 encoversized       encoded input over cap   -> REFUSED
 *   base64 malformed <text>   invalid alphabet byte    -> REFUSED
 *   base64 badlen <text>      length not a multiple of 4 -> REFUSED
 *
 * All index arithmetic uses shifts, bitmasks and named constants; there is no
 * variable division/modulus.  Teaching model, NOT a security proof.
 */
#include <stdio.h>
#include <string.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

#define B64_CAP     96u
#define B64_DEC     10u
#define B64_ENC_CAP 128u

static const char B64_ALPHA[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static error *b64_err;

static void b64_refuse(const char *what, cplus_exit_code code) {
    if (b64_err != NULL)
        error_fail(b64_err, code, what);
}

/* Alphabet index of one character, or -1 when it is not in the alphabet. */
static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int b64_encode(const unsigned char *in, size_t n,
                      char *out, size_t out_cap, size_t *out_len) {
    size_t i = 0, o = 0;
    while (i < n) {
        unsigned int b0 = (unsigned int)in[i];
        unsigned int b1 = (i + 1 < n) ? (unsigned int)in[i + 1] : 0u;
        unsigned int b2 = (i + 2 < n) ? (unsigned int)in[i + 2] : 0u;
        unsigned int t = (b0 << 16) | (b1 << 8) | b2;
        if (o + 4 > out_cap) {
            b64_refuse("encoded-too-long", CPLUS_EXIT_BOUNDS);
            return 0;
        }
        out[o++] = B64_ALPHA[(t >> 18) & 0x3Fu];
        out[o++] = B64_ALPHA[(t >> 12) & 0x3Fu];
        out[o++] = (i + 1 < n) ? B64_ALPHA[(t >> 6) & 0x3Fu] : '=';
        out[o++] = (i + 2 < n) ? B64_ALPHA[t & 0x3Fu] : '=';
        i += 3;
    }
    *out_len = o;
    return 1;
}

static int b64_decode(const char *in, size_t n,
                      unsigned char *out, size_t out_cap, size_t *out_len) {
    size_t i, o = 0;
    if (n == 0) {
        *out_len = 0;
        return 1;
    }
    if ((n & 3u) != 0) {
        b64_refuse("malformed-length", CPLUS_EXIT_UNSUPPORTED);
        return 0;
    }
    if (n > B64_ENC_CAP) {
        b64_refuse("encoded-too-long", CPLUS_EXIT_BOUNDS);
        return 0;
    }
    for (i = 0; i < n; i += 4) {
        char c2 = in[i + 2];
        char c3 = in[i + 3];
        int is_last = (i + 4 == n);
        int pad2 = (c2 == '=');
        int pad3 = (c3 == '=');
        int v0 = b64_val(in[i]);
        int v1 = b64_val(in[i + 1]);
        unsigned int t;

        if (v0 < 0 || v1 < 0) {
            b64_refuse("malformed-encoding", CPLUS_EXIT_UNSUPPORTED);
            return 0;
        }
        /* Padding may appear only in the final quartet, and only as "=="
         * or "=".  An '=' in position 3 forces '=' in position 4. */
        if ((pad2 && (!is_last || !pad3)) || (pad3 && !is_last)) {
            b64_refuse("malformed-padding", CPLUS_EXIT_UNSUPPORTED);
            return 0;
        }
        t = ((unsigned int)v0 << 18) | ((unsigned int)v1 << 12);
        if (!pad2) {
            int v2 = b64_val(c2);
            if (v2 < 0) {
                b64_refuse("malformed-encoding", CPLUS_EXIT_UNSUPPORTED);
                return 0;
            }
            t |= (unsigned int)v2 << 6;
        }
        if (!pad3) {
            int v3 = b64_val(c3);
            if (v3 < 0) {
                b64_refuse("malformed-encoding", CPLUS_EXIT_UNSUPPORTED);
                return 0;
            }
            t |= (unsigned int)v3;
        }
        if (o >= out_cap) {
            b64_refuse("decoded-too-long", CPLUS_EXIT_BOUNDS);
            return 0;
        }
        out[o++] = (unsigned char)((t >> 16) & 0xFFu);
        if (!pad2) {
            if (o >= out_cap) {
                b64_refuse("decoded-too-long", CPLUS_EXIT_BOUNDS);
                return 0;
            }
            out[o++] = (unsigned char)((t >> 8) & 0xFFu);
        }
        if (!pad2 && !pad3) {
            if (o >= out_cap) {
                b64_refuse("decoded-too-long", CPLUS_EXIT_BOUNDS);
                return 0;
            }
            out[o++] = (unsigned char)(t & 0xFFu);
        }
        if (!is_last && (pad2 || pad3)) {
            b64_refuse("malformed-padding", CPLUS_EXIT_UNSUPPORTED);
            return 0;
        }
    }
    *out_len = o;
    return 1;
}

static void b64_hex(const unsigned char *p, size_t n, char *out) {
    static const char h[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) {
        out[2 * i] = h[(p[i] >> 4) & 0x0Fu];
        out[2 * i + 1] = h[p[i] & 0x0Fu];
    }
    out[2 * n] = '\0';
}

static void b64_num(unsigned int v, char *out) {
    char rev[16];
    int k = 0, j = 0;
    if (v == 0) rev[k++] = '0';
    while (v != 0) {
        rev[k++] = (char)('0' + (v % B64_DEC));
        v = v / B64_DEC;
    }
    while (k > 0) out[j++] = rev[--k];
    out[j] = '\0';
}

/* Encode `plain`, decode the result, and require byte-exact equality. */
static int b64_case(const char *plain, string *line, vector *lens) {
    unsigned char in[B64_CAP + 2];
    unsigned char back[B64_CAP + 2];
    char enc[B64_ENC_CAP + 2];
    char num[24];
    size_t n = strlen(plain);
    size_t elen = 0, dlen = 0, i;

    if (n > B64_CAP) {
        b64_refuse("input-too-long", CPLUS_EXIT_BOUNDS);
        return 0;
    }
    for (i = 0; i < n; i++)
        in[i] = (unsigned char)plain[i];

    if (!b64_encode(in, n, enc, sizeof(enc), &elen)) return 0;
    if (!b64_decode(enc, elen, back, sizeof(back), &dlen)) return 0;
    if (dlen != n) {
        b64_refuse("roundtrip-length", CPLUS_EXIT_UNSUPPORTED);
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (back[i] != in[i]) {
            b64_refuse("roundtrip-mismatch", CPLUS_EXIT_UNSUPPORTED);
            return 0;
        }
    }
    enc[elen] = '\0';

    line->append("BASE64 enc \"");
    line->append(plain);
    line->append("\" -> ");
    line->append(enc);
    line->append(" rt=OK len=");
    b64_num((unsigned int)n, num);
    line->append(num);
    if (lens != NULL)
        lens->push_back((int)n);
    print(line->c_str());
    line->clear();
    return 1;
}

static char *b64_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

int main(int argc, char **argv) {
    error e = error();
    vector lens = vector();
    string line = string("");
    string mode = string("");
    int ok = 1;

    b64_err = &e;

    if (argc == 1) {
        ok = b64_case("", &line, &lens) &&
             b64_case("f", &line, &lens) &&
             b64_case("fo", &line, &lens) &&
             b64_case("foo", &line, &lens) &&
             b64_case("foob", &line, &lens) &&
             b64_case("fooba", &line, &lens) &&
             b64_case("foobar", &line, &lens);
        if (ok)
            print("BASE64 OK count=7 cap=96 n=", (int)lens.size());
    } else if (argc == 2) {
        char *m = b64_arg(argv, 1);
        mode = string(m);
        if (!mode.valid() || mode.size() > 24) {
            print("BASE64 REFUSED code=108 bad-mode");
            return 1;
        }
        if (mode.equals("oversized")) {
            char big[B64_CAP + 3];
            size_t i;
            for (i = 0; i < B64_CAP + 1; i++)
                big[i] = 'x';
            big[B64_CAP + 1] = '\0';
            ok = b64_case(big, &line, &lens);
        } else if (mode.equals("encoversized")) {
            char enc[B64_ENC_CAP + 5];
            unsigned char out[B64_CAP + 2];
            size_t i, dlen = 0;
            for (i = 0; i < B64_ENC_CAP + 4; i++)
                enc[i] = 'A';
            enc[B64_ENC_CAP + 4] = '\0';
            ok = b64_decode(enc, B64_ENC_CAP + 4, out, sizeof(out), &dlen);
        } else {
            b64_refuse("bad-mode", CPLUS_EXIT_UNSUPPORTED);
            ok = 0;
        }
    } else if (argc == 3) {
        char *m = b64_arg(argv, 1);
        char *v = b64_arg(argv, 2);
        mode = string(m);
        if (!mode.valid()) {
            b64_refuse("bad-mode", CPLUS_EXIT_UNSUPPORTED);
            ok = 0;
        } else if (mode.equals("enc")) {
            ok = b64_case(v, &line, &lens);
        } else if (mode.equals("dec")) {
            unsigned char out[B64_CAP + 2];
            char hex[B64_CAP + 2];
            char num[24];
            size_t n = strlen(v), dlen = 0;
            if (!b64_decode(v, n, out, sizeof(out), &dlen)) {
                ok = 0;
            } else {
                b64_hex(out, dlen, hex);
                line.append("BASE64 dec ");
                line.append(v);
                line.append(" len=");
                b64_num((unsigned int)dlen, num);
                line.append(num);
                line.append(" hex=");
                line.append(hex);
                print(line.c_str());
                line.clear();
            }
        } else if (mode.equals("malformed")) {
            unsigned char out[B64_CAP + 2];
            size_t dlen = 0;
            ok = b64_decode(v, strlen(v), out, sizeof(out), &dlen);
        } else if (mode.equals("badlen")) {
            unsigned char out[B64_CAP + 2];
            size_t dlen = 0;
            ok = b64_decode(v, strlen(v), out, sizeof(out), &dlen);
        } else {
            b64_refuse("bad-mode", CPLUS_EXIT_UNSUPPORTED);
            ok = 0;
        }
    } else {
        b64_refuse("usage", CPLUS_EXIT_UNSUPPORTED);
        ok = 0;
    }

    if (!ok) {
        if (e.failed_now())
            print("BASE64 REFUSED code=", e.code(), e.what());
        else
            print("BASE64 REFUSED code=108 internal");
    }

    line.drop();
    lens.drop();
    mode.drop();
    return ok ? 0 : 1;
}
