/* tbase64.cp -- bounded, byte-exact base64 encode / decode.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/utilities/text_tools/tbase64.cp --strict \
 *     --sanitize=none -I. -o apps/legacy_examples/utilities/text_tools/.run/tbase64
 *
 * Usage:
 *   tbase64 enc <path>     read raw bytes, print the base64 token
 *   tbase64 dec <path>     read a base64 token, print decoded length + hex
 *
 * Safety contract:
 *   - Raw input is capped at B64_MAX_IN bytes; one extra byte is detected and
 *     REFUSED, so no buffer index can exceed the fixed local array.
 *   - Decode strictly validates: length multiple of four, alphabet-only data,
 *     padding only at the tail, at most two '=', and canonical low bits.  Any
 *     violation is REFUSED with no partial output.
 *   - One trailing newline in a token file is tolerated; everything else
 *     outside the alphabet is malformed.
 *   - Output text is wrapped in a checked string before printing.
 *
 * Output (exact bytes, rc in parentheses):
 *   TBASE64 <token>          (0) enc success
 *   TBASE64 <count> <hex>    (0) dec success (lowercase hex, no separators)
 *   TBASE64 error usage      (2)
 *   TBASE64 error cannot-open (1)
 *   TBASE64 error resource   (1)
 *   TBASE64 error read       (1)
 *   TBASE64 refuse input-too-large (1)
 *   TBASE64 refuse malformed (1)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/file.hp"

#define B64_MAX_IN 4096
#define B64_TOKEN_CAP 5500

static char arg_alpha[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static char hex_alpha[] = "0123456789abcdef";

static char *arg(char **p, size_t i) {
    char *value;
    __builtin_memcpy(&value, p + i, sizeof(value));
    return value;
}

int main(int argc, char **argv) {
    error err = error();
    const char *mode;
    const char *path;
    char in[B64_MAX_IN + 1];
    char out[B64_TOKEN_CAP];
    char bytes[B64_MAX_IN];
    char hex[B64_MAX_IN * 2 + 1];
    int val[256];
    int n = 0;
    int rc = 0;
    int encoding;
    int i;

    if (argc != 3) {
        print("TBASE64 error usage");
        return 2;
    }
    mode = arg(argv, 1);
    if (strcmp(mode, "enc") == 0)
        encoding = 1;
    else if (strcmp(mode, "dec") == 0)
        encoding = 0;
    else {
        print("TBASE64 error usage");
        return 2;
    }
    path = arg(argv, 2);

    {
        string path_s = string(path);
        if (!path_s.valid()) {
            print("TBASE64 error resource");
            return 1;
        }
        path_s.drop();
    }

    {
        file stream = file(path, "r");
        int rr;
        if (!stream.is_open()) {
            print("TBASE64 error cannot-open");
            return 1;
        }
        for (;;) {
            char one[1];
            if (n >= B64_MAX_IN) {
                rr = stream.read(one, 1, &err);
                if (rr == STDLIB_EOF)
                    break;
                if (rr != STDLIB_OK) {
                    print("TBASE64 error read");
                    rc = 1;
                    break;
                }
                print("TBASE64 refuse input-too-large");
                rc = 1;
                break;
            }
            rr = stream.read(one, 1, &err);
            if (rr == STDLIB_EOF)
                break;
            if (rr != STDLIB_OK) {
                print("TBASE64 error read");
                rc = 1;
                break;
            }
            in[n] = one[0];
            n++;
        }
        stream.drop();
    }
    if (rc != 0)
        return 1;

    if (encoding) {
        int o = 0;
        i = 0;
        while (i < n) {
            unsigned int b0 = (unsigned int)(unsigned char)in[i];
            unsigned int b1 = i + 1 < n ? (unsigned int)(unsigned char)in[i + 1] : 0u;
            unsigned int b2 = i + 2 < n ? (unsigned int)(unsigned char)in[i + 2] : 0u;
            out[o] = arg_alpha[(b0 >> 2) & 63u];
            o++;
            out[o] = arg_alpha[((b0 & 3u) << 4) | ((b1 >> 4) & 15u)];
            o++;
            out[o] = i + 1 < n ? arg_alpha[((b1 & 15u) << 2) | ((b2 >> 6) & 3u)] : '=';
            o++;
            out[o] = i + 2 < n ? arg_alpha[b2 & 63u] : '=';
            o++;
            i += 3;
        }
        out[o] = '\0';
        {
            string os = string(out);
            if (!os.valid()) {
                print("TBASE64 error resource");
                return 1;
            }
            print("TBASE64", os.c_str());
            os.drop();
        }
        return 0;
    }

    /* Decode path: tolerate a single trailing newline from a text fixture. */
    while (n > 0 && (in[n - 1] == '\n' || in[n - 1] == '\r'))
        n--;
    if (n == 0 || (n % 4) != 0) {
        print("TBASE64 refuse malformed");
        return 1;
    }

    for (i = 0; i < 256; i++)
        val[i] = -1;
    for (i = 0; i < 64; i++)
        val[(unsigned char)arg_alpha[i]] = i;

    {
        int pads = 0;
        if (in[n - 1] == '=')
            pads++;
        if (n >= 2 && in[n - 2] == '=')
            pads++;
        if (pads > 2) {
            print("TBASE64 refuse malformed");
            return 1;
        }
        for (i = 0; i < n; i++) {
            unsigned char c = (unsigned char)in[i];
            if (c == '=') {
                if (i < n - pads) {
                    print("TBASE64 refuse malformed");
                    return 1;
                }
            } else if (val[c] < 0) {
                print("TBASE64 refuse malformed");
                return 1;
            }
        }
        /* One '=' leaves 2 unused bits in the 3rd sextet; two '=' leave
         * 4 unused bits in the 2nd sextet.  Both must be zero (RFC 4648). */
        if (pads == 1) {
            unsigned char c = (unsigned char)in[n - 2];
            if ((val[c] & 3) != 0) {
                print("TBASE64 refuse malformed");
                return 1;
            }
        } else if (pads == 2) {
            unsigned char c = (unsigned char)in[n - 3];
            if ((val[c] & 15) != 0) {
                print("TBASE64 refuse malformed");
                return 1;
            }
        }
    }

    {
        int m = 0;
        /* Decode four sextets at a time so every shift count is a literal:
         * the strict checker cannot prove a variable shift stays in range. */
        for (i = 0; i < n; i += 4) {
            /* A padding byte contributes a sextet VALUE of zero, not index
             * zero into val[] (which is deliberately -1 for NUL). */
            int v0 = val[(unsigned char)in[i]];
            int v1 = val[(unsigned char)in[i + 1]];
            int v2 = in[i + 2] == '=' ? 0 : val[(unsigned char)in[i + 2]];
            int v3 = in[i + 3] == '=' ? 0 : val[(unsigned char)in[i + 3]];
            unsigned int g = ((unsigned int)v0 << 18) |
                             ((unsigned int)v1 << 12) |
                             ((unsigned int)v2 << 6) |
                             (unsigned int)v3;
            bytes[m] = (char)((g >> 16) & 255u);
            m++;
            if (in[i + 2] != '=') {
                bytes[m] = (char)((g >> 8) & 255u);
                m++;
            }
            if (in[i + 3] != '=') {
                bytes[m] = (char)(g & 255u);
                m++;
            }
        }
        {
            int expected = (n / 4) * 3;
            if (in[n - 1] == '=')
                expected--;
            if (n >= 2 && in[n - 2] == '=')
                expected--;
            if (m != expected) {
                print("TBASE64 refuse malformed");
                return 1;
            }
        }
        for (i = 0; i < m; i++) {
            unsigned char c = (unsigned char)bytes[i];
            hex[2 * i] = hex_alpha[(c >> 4) & 15];
            hex[2 * i + 1] = hex_alpha[c & 15];
        }
        hex[2 * m] = '\0';
        {
            string os = string(hex);
            if (!os.valid()) {
                print("TBASE64 error resource");
                return 1;
            }
            print("TBASE64", m, os.c_str());
            os.drop();
        }
    }
    return 0;
}
