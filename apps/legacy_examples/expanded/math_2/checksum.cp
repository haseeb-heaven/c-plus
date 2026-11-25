/* checksum.cp -- CRC32 + Adler32 over bounded, fixed byte strings.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/math_2/checksum.cp --strict --sanitize=none -I. \
 *       -o examples/expanded/math_2/.run/checksum
 *
 * Usage:
 *   checksum                 built-in deterministic vectors
 *   checksum text <ascii>    checksum one fixed ASCII argument
 *   checksum oversized       fixed 129-byte input -> REFUSED (input-too-long)
 *
 * CRC32 is the reflected IEEE polynomial 0xEDB88320 (bitwise, no table).
 * Adler32 uses the named prime modulus 65521.  No variable divisor/modulus
 * and no variable-width shift are used.  Teaching model, NOT a proof.
 */
#include <stdio.h>
#include <string.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

#define CK_CAP       128u
#define CK_DEC       10u
#define CRC_POLY     0xEDB88320u
#define ADLER_MOD    65521u
#define CRC_SEED     0xFFFFFFFFu

static error *ck_err;

static void ck_refuse(const char *what, cplus_exit_code code) {
    if (ck_err != NULL)
        error_fail(ck_err, code, what);
}

static unsigned int ck_crc32(const char *data, size_t n) {
    unsigned int crc = CRC_SEED;
    size_t i;
    int b;
    for (i = 0; i < n; i++) {
        crc ^= (unsigned int)(unsigned char)data[i];
        for (b = 0; b < 8; b++) {
            unsigned int mask = (unsigned int)-(int)(crc & 1u);
            crc = (crc >> 1) ^ (CRC_POLY & mask);
        }
    }
    return crc ^ CRC_SEED;
}

static unsigned int ck_adler32(const char *data, size_t n) {
    unsigned int a = 1u, b = 0u;
    size_t i;
    for (i = 0; i < n; i++) {
        a = (a + (unsigned int)(unsigned char)data[i]) % ADLER_MOD;
        b = (b + a) % ADLER_MOD;
    }
    return (b << 16) | a;
}

static void ck_hex(unsigned int v, char *out) {
    static const char digits[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 8; i++) {
        out[i] = digits[(v >> 28) & 0xFu];
        v <<= 4;
    }
    out[8] = '\0';
}

/* Copy into a fixed buffer only after the explicit capacity check. */
static int ck_load(const char *text, size_t n, char *buf, size_t buf_size) {
    size_t i;
    if (n > CK_CAP || n + 1 > buf_size) {
        ck_refuse("input-too-long", CPLUS_EXIT_BOUNDS);
        return 0;
    }
    for (i = 0; i < n; i++)
        buf[i] = text[i];
    buf[n] = '\0';
    return 1;
}

static int ck_case(const char *text, string *line, vector *lengths) {
    char buf[CK_CAP + 2];
    char hc[9], ha[9];
    size_t n = strlen(text);
    if (!ck_load(text, n, buf, sizeof(buf)))
        return 0;
    ck_hex(ck_crc32(buf, n), hc);
    ck_hex(ck_adler32(buf, n), ha);

    line->append("CHECKSUM len=");
    {
        char num[24];
        size_t k = 0, j = 0;
        unsigned int v = (unsigned int)n;
        char rev[16];
        if (v == 0) rev[k++] = '0';
        while (v != 0) {
            rev[k++] = (char)('0' + (v % CK_DEC));
            v = v / CK_DEC;
        }
        while (k > 0) num[j++] = rev[--k];
        num[j] = '\0';
        line->append(num);
    }
    line->append(" text=");
    line->append(buf);
    line->append(" crc32=");
    line->append(hc);
    line->append(" adler32=");
    line->append(ha);
    if (lengths != NULL)
        lengths->push_back((int)n);
    print(line->c_str());
    line->clear();
    return 1;
}

static char *ck_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

int main(int argc, char **argv) {
    error e = error();
    vector lengths = vector();
    string line = string("");
    string mode = string("");
    int ok = 1;

    ck_err = &e;

    if (argc == 1) {
        ok = ck_case("", &line, &lengths) &&
             ck_case("123456789", &line, &lengths) &&
             ck_case("The quick brown fox jumps over the lazy dog", &line, &lengths) &&
             ck_case("abc", &line, &lengths);
        if (ok)
            print("CHECKSUM OK count=4 cap=128 n=", (int)lengths.size());
    } else if (argc == 2) {
        char *m = ck_arg(argv, 1);
        mode = string(m);
        if (!mode.valid() || mode.size() > 32) {
            print("CHECKSUM REFUSED code=101 bad-mode");
            return 1;
        }
        if (mode.equals("oversized")) {
            char big[CK_CAP + 3];
            size_t i;
            for (i = 0; i < CK_CAP + 1; i++)
                big[i] = 'A';
            big[CK_CAP + 1] = '\0';
            ok = ck_case(big, &line, &lengths);
        } else {
            ck_refuse("bad-mode", CPLUS_EXIT_UNSUPPORTED);
            ok = 0;
        }
    } else if (argc == 3) {
        char *m = ck_arg(argv, 1);
        char *t = ck_arg(argv, 2);
        mode = string(m);
        if (!mode.valid() || !mode.equals("text")) {
            ck_refuse("bad-mode", CPLUS_EXIT_UNSUPPORTED);
            ok = 0;
        } else {
            ok = ck_case(t, &line, &lengths);
        }
    } else {
        ck_refuse("usage", CPLUS_EXIT_UNSUPPORTED);
        ok = 0;
    }

    if (!ok) {
        if (e.failed_now())
            print("CHECKSUM REFUSED code=", e.code(), e.what());
        else
            print("CHECKSUM REFUSED code=108 internal");
    }

    line.drop();
    lengths.drop();
    mode.drop();
    return ok ? 0 : 1;
}
