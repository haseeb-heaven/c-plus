/* firewall.cp -- deterministic, teaching-only input-validation firewall.
 *
 * This is NOT a security product.  It is a small, fully deterministic model
 * of an allow-list "firewall" layer written in C+ on top of the checked
 * stdlib (stdlib/string.hp + stdlib/error.hp).  Every run is a pure function
 * of its argv tokens: no clock, no randomness, no files, no network, no
 * interactive input, and only bounded loops over a fixed-size buffer.
 *
 * Invocation (from the repository root):
 *   firewall <policy> <input>
 *     policy = token | pathseg | sqlsafe | text
 *     input  = one candidate token (may be empty)
 *
 * Output (all lines are fixed-format, hence byte-comparable):
 *   CONTRAST ...                         what an UNCHECKED implementation would do
 *   OK <sanitized>                       accepted (exit 0)
 *   REFUSED <rule> <code>                rejected (exit 2)
 *   DETAIL ...
 *   FIREWALL verdict=... rule=... code=... errcode=...
 *
 * The CONTRAST line is the runtime evidence for the "unchecked path" side of
 * the demonstration.  For the SAME argv token it reports what a naive
 * `char buf[32]; strcpy(buf, input);` write would do (derived from the real,
 * bounded input length), plus a flavour-specific consequence for path
 * traversal, SQL injection, NUL truncation and control-byte passthrough.  The
 * checked layer then refuses the identical token.  All contrast values are
 * computed at run time from the actual input; none are hard-coded.
 *
 * NULL bytes cannot survive in an argv C string, so NUL injection is modelled
 * with the textual markers `%00`, `\0` and `\x00`.  See NOTES.md.
 */
#include <stdio.h>
#include <string.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

/* Fixed capacities: every scan is bounded by a compile-time constant. */
#define FW_SCAN_CAP   512
#define FW_BUF_CAP    (FW_SCAN_CAP + 1)
#define FW_MAX_LEN    256
#define FW_CLEAN_CAP  (FW_MAX_LEN + 1)
#define FW_NAIVE_BUF  32
#define FW_NAIVE_CAP  (FW_SCAN_CAP + 64)

struct fw_result {
    int ok;
    const char *rule;
    const char *code;
    const char *message;
};

/* The checked stdlib error record behind every refusal.  A plain global
 * pointer keeps the C+ method-call desugaring simple (chained `r->err->set`
 * is not a supported receiver form in this toolchain). */
static error *g_fw_err = (error *)0;

/* Read one byte from an FFI pointer without subscripting it.  Strict mode
 * rejects argv[variable]; a sized builtin copy makes the extent explicit. */
static char fw_byte(const char *p, size_t i) {
    char c;
    __builtin_memcpy(&c, p + i, sizeof(c));
    return c;
}

/* Bounded copy of an FFI string into a local buffer of FW_BUF_CAP bytes. */
static size_t fw_read_bounded(const char *src, char *dst, size_t cap) {
    size_t i = 0;
    if (src == (const char *)0) {
        dst[0] = '\0';
        return 0;
    }
    while (i < cap) {
        char c = fw_byte(src, i);
        if (c == '\0')
            break;
        dst[i] = c;
        i++;
    }
    dst[i] = '\0';
    return i;
}

/* Fetch an argv slot as a plain pointer, then copy it into owned storage. */
static char *fw_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static void fw_refuse(struct fw_result *r, const char *rule, const char *code,
                      const char *msg, int category, cplus_exit_code ec) {
    r->ok = 0;
    r->rule = rule;
    r->code = code;
    r->message = msg;
    if (g_fw_err != (error *)0)
        g_fw_err->set_category(category, ec, msg);
}

static void fw_accept(struct fw_result *r) {
    r->ok = 1;
    r->rule = "-";
    r->code = "-";
    r->message = "accepted";
    if (g_fw_err != (error *)0)
        g_fw_err->clear();
}

/* Lowercase a bounded owned string into out (size >= FW_BUF_CAP). */
static void fw_lower(string *s, char *out, size_t cap) {
    size_t i;
    size_t n = s->size();
    if (n + 1 > cap)
        n = cap - 1;
    for (i = 0; i < n; i++) {
        char c = s->at(i);
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        out[i] = c;
    }
    out[n] = '\0';
}

/* The ordered firewall.  The first fired rule wins, so output is stable. */
static void fw_validate(const char *raw, size_t n, const char *policy,
                        struct fw_result *r) {
    string input = string(raw);
    char lowerbuf[FW_BUF_CAP];
    string low = string("");
    size_t i;
    int k;

    fw_refuse(r, "storage", "E_MEMORY", "checked string unavailable",
              STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE);
    if (!input.valid()) {
        input.drop();
        return;
    }

    /* 1. empty input */
    if (n == 0) {
        fw_refuse(r, "no-empty", "E_EMPTY", "input is empty",
                  STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED);
        input.drop();
        return;
    }

    /* 2. oversized input (hard cap of 256 bytes) */
    if (n > FW_MAX_LEN) {
        fw_refuse(r, "max-len", "E_SIZE", "input exceeds 256 bytes",
                  STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS);
        input.drop();
        return;
    }

    /* Build a bounded case-folded view for marker searches. */
    fw_lower(&input, lowerbuf, sizeof(lowerbuf));
    low = string(lowerbuf);
    if (!low.valid()) {
        fw_refuse(r, "storage", "E_MEMORY", "case-fold buffer unavailable",
                  STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE);
        low.drop();
        input.drop();
        return;
    }

    /* 3. NUL-byte injection, modelled with textual markers */
    if (input.find("%00") >= 0 || input.find("\\0") >= 0 ||
        input.find("\\x00") >= 0 || low.find("%u0000") >= 0) {
        fw_refuse(r, "no-nul", "E_NUL", "NUL byte injection marker",
                  STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED);
        low.drop();
        input.drop();
        return;
    }

    /* 4. control characters (C0 controls and DEL) */
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)input.at(i);
        if (c < 0x20 || c == 0x7f) {
            fw_refuse(r, "no-control", "E_CONTROL", "control character",
                      STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED);
            low.drop();
            input.drop();
            return;
        }
    }

    /* 5. path traversal (bounded substring checks; no regex) */
    if (input.find("../") >= 0 || input.find("..\\") >= 0 ||
        input.find("/..") >= 0 || input.find("\\..") >= 0 ||
        input.equals("..")) {
        fw_refuse(r, "no-traversal", "E_TRAVERSAL", "path traversal sequence",
                  STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED);
        low.drop();
        input.drop();
        return;
    }

    /* 6. SQL-like injection markers (' OR 1=1 and friends) */
    if (low.find("or 1=1") >= 0 || low.find("' or") >= 0 ||
        low.find("union select") >= 0 || low.find("drop table") >= 0 ||
        input.find("'") >= 0 || input.find("\"") >= 0 ||
        input.find(";") >= 0 || input.find("--") >= 0 ||
        input.find("/*") >= 0 || input.find("*/") >= 0) {
        fw_refuse(r, "no-sqli", "E_SQLI", "SQL injection marker",
                  STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED);
        low.drop();
        input.drop();
        return;
    }

    /* 7. policy-specific allow-list / character pattern */
    if (strcmp(policy, "token") == 0) {
        static const char *allow[5] = {"alpha", "beta", "gamma", "report",
                                       "status"};
        int found = 0;
        for (k = 0; k < 5; k++)
            if (strcmp(raw, allow[k]) == 0)
                found = 1;
        if (!found) {
            fw_refuse(r, "allowlist", "E_NOTALLOWED", "token not in allow-list",
                      STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED);
            low.drop();
            input.drop();
            return;
        }
    } else if (strcmp(policy, "pathseg") == 0) {
        for (i = 0; i < n; i++) {
            unsigned char c = (unsigned char)input.at(i);
            int good = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '_' || c == '-' ||
                       c == '.';
            if (!good) {
                fw_refuse(r, "pattern", "E_NOTALLOWED",
                          "character outside path-segment pattern",
                          STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED);
                low.drop();
                input.drop();
                return;
            }
        }
    } else if (strcmp(policy, "sqlsafe") == 0) {
        for (i = 0; i < n; i++) {
            unsigned char c = (unsigned char)input.at(i);
            int good = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '_' || c == ' ';
            if (!good) {
                fw_refuse(r, "pattern", "E_NOTALLOWED",
                          "character outside sqlsafe pattern",
                          STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED);
                low.drop();
                input.drop();
                return;
            }
        }
    } else if (strcmp(policy, "text") == 0) {
        for (i = 0; i < n; i++) {
            unsigned char c = (unsigned char)input.at(i);
            if (c < 0x20 || c > 0x7e) {
                fw_refuse(r, "pattern", "E_NOTALLOWED", "non-printable byte",
                          STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED);
                low.drop();
                input.drop();
                return;
            }
        }
    } else {
        fw_refuse(r, "usage", "E_POLICY", "unknown policy",
                  STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED);
        low.drop();
        input.drop();
        return;
    }

    fw_accept(r);
    low.drop();
    input.drop();
}

/* Trim ASCII spaces on both ends, then copy through the checked substr(). */
static void fw_sanitize(string *input, char *out, size_t cap) {
    size_t a = 0;
    size_t b = input->size();
    while (a < b && input->at(a) == ' ')
        a++;
    while (b > a && input->at(b - 1) == ' ')
        b--;
    (void)input->substr(a, b - a, out, cap);
}

/* Runtime evidence for the "ordinary unchecked path" half of the demo.
 * Every number/string below is computed from THIS run's bounded input. */
static void fw_contrast(const char *policy, string *input,
                        const struct fw_result *r) {
    size_t n = input->size();
    int writes = (int)n + 1;
    int overflow = writes - FW_NAIVE_BUF;
    char naive[FW_NAIVE_CAP];

    printf("CONTRAST policy=%s naive=strcpy32 writes=%d buffer=%d overflow=%d\n",
           policy, writes, FW_NAIVE_BUF, overflow);

    if (strcmp(r->code, "E_TRAVERSAL") == 0) {
        (void)snprintf(naive, sizeof(naive), "/srv/app/data/%s",
                       input->c_str());
        printf("CONTRAST flavor=path-concat unchecked=accept escapes_base=1 "
               "path=%s\n", naive);
    } else if (strcmp(r->code, "E_SQLI") == 0) {
        (void)snprintf(naive, sizeof(naive),
                       "SELECT * FROM users WHERE name='%s'", input->c_str());
        printf("CONTRAST flavor=sql-format unchecked=accept always_true=1 "
               "query=%s\n", naive);
    } else if (strcmp(r->code, "E_NUL") == 0) {
        long pos = input->find("%00");
        size_t cut;
        if (pos < 0)
            pos = input->find("\\0");
        if (pos < 0)
            pos = input->find("\\x00");
        cut = pos < 0 ? 0 : (size_t)pos;
        if (cut > n)
            cut = n;
        if (cut > (size_t)(FW_NAIVE_CAP - 1))
            cut = (size_t)(FW_NAIVE_CAP - 1);
        (void)input->substr(0, cut, naive, sizeof(naive));
        printf("CONTRAST flavor=nul-truncate unchecked=accept visible=%s "
               "os_sees_only_prefix=1\n", naive);
    } else if (strcmp(r->code, "E_CONTROL") == 0) {
        size_t i;
        int ctrl = 0;
        for (i = 0; i < n; i++) {
            unsigned char c = (unsigned char)input->at(i);
            if (c < 0x20 || c == 0x7f)
                ctrl++;
        }
        printf("CONTRAST flavor=log-passthrough unchecked=accept "
               "control_bytes=%d emitted_to_terminal=1\n", ctrl);
    } else if (strcmp(r->code, "E_SIZE") == 0) {
        printf("CONTRAST flavor=unbounded-copy unchecked=corrupts "
               "overrun_bytes=%d\n", overflow);
    } else if (r->ok) {
        printf("CONTRAST flavor=normal unchecked=accept safe_for_this_input=1\n");
    } else {
        printf("CONTRAST flavor=policy unchecked=accept "
               "unknown_input_would_pass=1\n");
    }
    fflush(stdout);
}

int main(int argc, char **argv) {
    const char *policy;
    char raw[FW_BUF_CAP];
    char clean[FW_CLEAN_CAP];
    size_t n;
    struct fw_result r;
    error ferr = error();

    if (argc < 3) {
        printf("FIREWALL usage: firewall <token|pathseg|sqlsafe|text> <input>\n");
        fflush(stdout);
        return 3;
    }
    policy = fw_arg(argv, (size_t)1);
    n = fw_read_bounded(fw_arg(argv, (size_t)2), raw, FW_SCAN_CAP);

    r.ok = 0;
    r.rule = "none";
    r.code = "E_NONE";
    r.message = "not evaluated";
    g_fw_err = &ferr;
    fw_validate(raw, n, policy, &r);

    {
        string shown = string(raw);
        fw_contrast(policy, &shown, &r);
        if (r.ok) {
            fw_sanitize(&shown, clean, sizeof(clean));
            printf("OK %s\n", clean);
        } else {
            printf("REFUSED %s %s\n", r.rule, r.code);
        }
        printf("DETAIL policy=%s message=%s errcode=%d\n", policy, r.message,
               ferr.code());
        printf("FIREWALL verdict=%s rule=%s code=%s errcode=%d\n",
               r.ok ? "OK" : "REFUSED", r.rule, r.code, ferr.code());
        fflush(stdout);
        shown.drop();
    }

    return r.ok ? 0 : 2;
}
