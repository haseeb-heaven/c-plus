/* vault.cp -- a TEACHING credential vault whose safety comes from refusals.
 *
 * This is NOT a production vault.  It is an in-memory, deterministic model
 * built to make C+ checked-stdlib refusals observable at runtime.  Everything
 * lives in fixed-capacity tables; no file, network, clock, prompt, randomness
 * or real credential is involved.  Secrets are synthetic argv tokens.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/secure_apps/vault/vault.cp \
 *     --strict --sanitize=none -I. \
 *     -o examples/secure_apps/vault/vault
 *
 * Commands (argv only; "--" chains several commands inside one process so a
 * whole transcript is one deterministic run):
 *   vault add <service> <secret>    store a credential (secret never echoed)
 *   vault get <service>             THE ONLY command that emits a secret
 *   vault list                      index + service names, never secrets
 *   vault peek <index>              service name at an index (never a secret)
 *   vault delete <service>          remove a credential by service name
 *   vault debug <service>           REFUSED: logging a secret is forbidden
 *   vault audit                     print the bounded audit buffer
 *
 * Refusals (each sets a typed `error *` from stdlib/error.hp and prints a
 * stable "VAULT refuse <reason> code <n> cat <name>" line):
 *   duplicate-service, empty-name, empty-secret,
 *   oversized-name, oversized-secret, missing-service,
 *   index-out-of-range, vault-full, secret-logging-forbidden,
 *   audit-overflow, bad-args, unknown-command, usage.
 *
 * Size policy: service <= 16 bytes, secret <= 32 bytes, <= 8 credentials.
 * All name/secret lengths are measured with stdlib/string.hp; copies go
 * through string::substr into fixed-size storage.  Audit records are fixed
 * 31-byte lines in a 512-byte buffer (16 records); a 17th append is refused
 * with CPLUS_EXIT_RESOURCE and the buffer is left unchanged (never a silent
 * drop).  A 64-byte canary around the tables is checked on every exit.
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

#define SV_MAX          8
#define SV_NAME_MAX     16
#define SV_SECRET_MAX   32
#define SV_NAME_CAP     17   /* 16 payload bytes + NUL */
#define SV_SECRET_CAP   33   /* 32 payload bytes + NUL */

#define AUDIT_REC_LEN   31   /* "%04u %-8s %-16s" = 30 chars + NUL */
#define AUDIT_CAP       512  /* 16 records, exactly */
#define CANARY_LEN      64

static char sv_names[SV_MAX][SV_NAME_CAP];
static char sv_secrets[SV_MAX][SV_SECRET_CAP];
static int sv_count;

static char g_audit[AUDIT_CAP];
static size_t g_audit_used;
static unsigned int g_audit_seq;

static unsigned char g_canary[CANARY_LEN];
static error *g_err;
static int g_ops;
static int g_refusals;

/* argv is an FFI pointer: copy each slot with memcpy so --strict can prove it. */
static char *sv_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static unsigned char sv_canary_byte(size_t i) {
    return (unsigned char)((0x5A ^ i) & 0xFFu);
}

static void sv_canary_fill(void) {
    size_t i;
    for (i = 0; i < CANARY_LEN; i++)
        g_canary[i] = sv_canary_byte(i);
}

static int sv_canary_check(void) {
    size_t i;
    for (i = 0; i < CANARY_LEN; i++)
        if (g_canary[i] != sv_canary_byte(i))
            return 0;
    return 1;
}

/* Every refusal is a typed error: code + stable category + reason on stdout. */
static int sv_refuse(int category, cplus_exit_code code, const char *reason) {
    error_fail_cat(g_err, category, code, reason);
    print("VAULT refuse", reason, "code", g_err->code(),
          "cat", stdlib_category_name(g_err->category()));
    g_refusals++;
    return 1;
}

/* Length measured through the checked stdlib string, never raw truncation. */
static size_t sv_length(const char *src) {
    string s = string(src); /* NULL ctor is documented as "" */
    size_t n = s.valid() ? s.size() : 0;
    s.drop();
    return n;
}

/* Checked copy: only called after the length has been validated. */
static int sv_copy_bounded(const char *src, char *dst, size_t cap,
                           size_t max_len) {
    string s = string(src);
    size_t n;
    int ok = 0;
    if (s.valid()) {
        n = s.size();
        if (n > 0 && n <= max_len && n + 1 <= cap) {
            if (s.substr(0, n, dst, cap) == 0)
                ok = 1;
        }
    }
    s.drop();
    return ok;
}

/* Bounded projection of a name for an audit record (never a secret). */
static void sv_audit_name(const char *src, char *out, size_t cap) {
    string s = string(src);
    if (s.valid())
        s.substr(0, (size_t)SV_NAME_MAX, out, cap);
    else
        out[0] = '\0';
    s.drop();
}

/* Append one fixed-format record.  On overflow: typed refusal, no write. */
static int sv_audit_append(const char *op, const char *name) {
    char rec[AUDIT_REC_LEN];
    int written;
    size_t n;

    if (g_audit_used > AUDIT_CAP)
        return sv_refuse(STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE,
                         "audit-overflow");
    written = snprintf(rec, sizeof(rec), "%04u %-8s %-16s",
                       g_audit_seq, op, name);
    if (written < 0 || (size_t)written >= sizeof(rec))
        return sv_refuse(STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE,
                         "audit-overflow");
    n = (size_t)written;
    if (n + 1 > AUDIT_CAP - g_audit_used)
        return sv_refuse(STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE,
                         "audit-overflow");
    __builtin_memcpy(g_audit + g_audit_used, rec, n + 1);
    g_audit_used += n + 1;
    g_audit_seq++;
    return 0;
}

static void sv_audit_print(void) {
    char rec[AUDIT_REC_LEN];
    int i;
    print("VAULT audit bytes", g_audit_used, "cap", AUDIT_CAP);
    for (i = 0; i < AUDIT_CAP / AUDIT_REC_LEN; i++) {
        size_t off = (size_t)i * AUDIT_REC_LEN;
        if (off + AUDIT_REC_LEN <= g_audit_used) {
            __builtin_memcpy(rec, g_audit + off, AUDIT_REC_LEN);
            print("VAULT audit-line", rec);
        }
    }
}

static int sv_parse_int(const char *text, int *out) {
    char *end = (char *)text;
    long v;
    if (text == NULL)
        return 0;
    errno = 0;
    v = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE ||
        v < INT_MIN || v > INT_MAX)
        return 0;
    *out = (int)v;
    return 1;
}

static int sv_find(const char *name) {
    int i;
    for (i = 0; i < SV_MAX && i < sv_count; i++)
        if (strcmp(sv_names[i], name) == 0)
            return i;
    return -1;
}

static int sv_add(const char *name_src, const char *secret_src) {
    char name[SV_NAME_CAP];
    char secret[SV_SECRET_CAP];
    size_t nl;
    size_t sl;
    size_t slot;

    nl = sv_length(name_src);
    sl = sv_length(secret_src);
    if (nl == 0)
        return sv_refuse(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                         "empty-name");
    if (sl == 0)
        return sv_refuse(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                         "empty-secret");
    if (nl > (size_t)SV_NAME_MAX)
        return sv_refuse(STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                         "oversized-name");
    if (sl > (size_t)SV_SECRET_MAX)
        return sv_refuse(STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                         "oversized-secret");
    if (!sv_copy_bounded(name_src, name, sizeof(name), (size_t)SV_NAME_MAX))
        return sv_refuse(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                         "name-copy-failed");
    if (!sv_copy_bounded(secret_src, secret, sizeof(secret),
                         (size_t)SV_SECRET_MAX))
        return sv_refuse(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                         "secret-copy-failed");
    if (sv_find(name) >= 0)
        return sv_refuse(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                         "duplicate-service");
    if (sv_count >= SV_MAX)
        return sv_refuse(STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                         "vault-full");
    slot = (size_t)sv_count;
    if (slot >= (size_t)SV_MAX)
        return sv_refuse(STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                         "vault-full");
    __builtin_memcpy(sv_names[slot], name, SV_NAME_CAP);
    __builtin_memcpy(sv_secrets[slot], secret, SV_SECRET_CAP);
    sv_count++;
    print("VAULT ok add", name);
    return 0;
}

static int sv_get(const char *name) {
    int pos = sv_find(name);
    if (pos < 0)
        return sv_refuse(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                         "missing-service");
    if (pos >= SV_MAX)
        return sv_refuse(STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                         "index-out-of-range");
    /* The single, explicit secret emission in this program. */
    print("VAULT get", sv_names[pos], sv_secrets[pos]);
    return 0;
}

static void sv_list(void) {
    int i;
    print("VAULT list", sv_count);
    for (i = 0; i < SV_MAX && i < sv_count; i++)
        print("VAULT entry", i, sv_names[i]); /* never the secret */
}

static int sv_peek(const char *index_text) {
    int idx = 0;
    if (!sv_parse_int(index_text, &idx))
        return sv_refuse(STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                         "index-out-of-range");
    if (idx < 0 || idx >= sv_count || idx >= SV_MAX)
        return sv_refuse(STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                         "index-out-of-range");
    print("VAULT peek", idx, sv_names[idx]); /* never the secret */
    return 0;
}

static int sv_delete(const char *name) {
    int pos = sv_find(name);
    int i;
    if (pos < 0)
        return sv_refuse(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                         "missing-service");
    for (i = pos; i + 1 < SV_MAX && i + 1 < sv_count; i++) {
        __builtin_memcpy(sv_names[i], sv_names[i + 1], SV_NAME_CAP);
        __builtin_memcpy(sv_secrets[i], sv_secrets[i + 1], SV_SECRET_CAP);
    }
    sv_count--;
    print("VAULT ok delete", name);
    return 0;
}

/* A debug logging attempt is refused by design; no secret is ever read. */
static int sv_debug(void) {
    return sv_refuse(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                     "secret-logging-forbidden");
}

/* Only security-relevant operations are audited: add/delete/get/debug.
 * Pure introspection (list/peek/audit) never appends, so the audit log can
 * always be read even when it is full. */
static int sv_audited(const char *cmd) {
    return strcmp(cmd, "add") == 0 || strcmp(cmd, "delete") == 0 ||
           strcmp(cmd, "get") == 0 || strcmp(cmd, "debug") == 0;
}

static int sv_execute(const char *cmd, const char *a1, const char *a2) {
    char aname[SV_NAME_CAP];
    const char *op = "unknown";

    error_reset(g_err);
    g_ops++;

    if (cmd == NULL) {
        return sv_refuse(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                         "usage");
    }

    if (strcmp(cmd, "add") == 0)
        op = "add";
    else if (strcmp(cmd, "delete") == 0)
        op = "delete";
    else if (strcmp(cmd, "get") == 0)
        op = "get";
    else if (strcmp(cmd, "debug") == 0)
        op = "debug";
    else if (strcmp(cmd, "list") == 0)
        op = "list";
    else if (strcmp(cmd, "peek") == 0)
        op = "peek";
    else if (strcmp(cmd, "audit") == 0)
        op = "audit";

    /* Fail closed: a mutation or secret read without an audit record never
     * happens.  The bounded name projection keeps records fixed-format. */
    if (sv_audited(cmd)) {
        sv_audit_name(a1, aname, sizeof(aname));
        if (sv_audit_append(op, aname) != 0)
            return 1;
    }

    if (strcmp(cmd, "add") == 0) {
        if (a1 == NULL || a2 == NULL)
            return sv_refuse(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                             "bad-args");
        return sv_add(a1, a2);
    }
    if (strcmp(cmd, "get") == 0) {
        if (a1 == NULL || a2 != NULL)
            return sv_refuse(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                             "bad-args");
        return sv_get(a1);
    }
    if (strcmp(cmd, "delete") == 0) {
        if (a1 == NULL || a2 != NULL)
            return sv_refuse(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                             "bad-args");
        return sv_delete(a1);
    }
    if (strcmp(cmd, "peek") == 0) {
        if (a1 == NULL || a2 != NULL)
            return sv_refuse(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                             "bad-args");
        return sv_peek(a1);
    }
    if (strcmp(cmd, "debug") == 0) {
        if (a1 == NULL || a2 != NULL)
            return sv_refuse(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                             "bad-args");
        return sv_debug();
    }
    if (strcmp(cmd, "list") == 0) {
        if (a1 != NULL)
            return sv_refuse(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                             "bad-args");
        sv_list();
        return 0;
    }
    if (strcmp(cmd, "audit") == 0) {
        if (a1 != NULL)
            return sv_refuse(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                             "bad-args");
        sv_audit_print();
        return 0;
    }
    return sv_refuse(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                     "unknown-command");
}

int main(int argc, char **argv) {
    error ferr = error();
    int i = 1;
    int rc = 0;

    g_err = &ferr;
    sv_canary_fill();
    stdlib_set_error_callback(NULL); /* no secret-bearing callbacks */

    if (argc < 2) {
        sv_refuse(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED, "usage");
        rc = 2;
    } else {
        while (i < argc) {
            const char *cmd = sv_arg(argv, (size_t)i);
            const char *a1 = NULL;
            const char *a2 = NULL;
            i++;
            if (i < argc && strcmp(sv_arg(argv, (size_t)i), "--") != 0) {
                a1 = sv_arg(argv, (size_t)i);
                i++;
            }
            if (i < argc && strcmp(sv_arg(argv, (size_t)i), "--") != 0) {
                a2 = sv_arg(argv, (size_t)i);
                i++;
            }
            while (i < argc && strcmp(sv_arg(argv, (size_t)i), "--") != 0) {
                sv_refuse(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                          "bad-args");
                rc = 1;
                i++;
            }
            if (i < argc)
                i++;
            if (sv_execute(cmd, a1, a2) != 0)
                rc = 1;
        }
    }

    print("VAULT summary ops", g_ops, "refused", g_refusals,
          "audit-bytes", g_audit_used,
          "canary", sv_canary_check() ? "ok" : "broken");
    if (!sv_canary_check())
        rc = 1;
    return rc;
}
