/* vault.cp -- deterministic in-memory password vault with refusal proofs.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/apps_2/vault.cp \
 *     --strict --sanitize=none -I. \
 *     -o examples/expanded/apps_2/.run/vault
 *
 * Commands (argv only; "--" chains several commands in one process):
 *   vault add <service> <password>
 *   vault get <service>
 *   vault list
 *   vault delete <service>
 *
 * State is kept in fixed-capacity tables. Service names and passwords are
 * copied with the checked `stdlib/string.hp` API; every refusal also records a
 * typed `error *` (CPLUS_EXIT_UNSUPPORTED / CPLUS_EXIT_BOUNDS). Secrets are
 * printed ONLY from the explicit `get` command -- `add`, `list` and `delete`
 * never emit a password. No clock, no prompts, no files, no shell.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

#define VAULT_MAX      16
#define VAULT_FIELD    257   /* 256 payload bytes + NUL */
#define VAULT_LIMIT    256

static char vault_names[VAULT_MAX][VAULT_FIELD];
static char vault_secrets[VAULT_MAX][VAULT_FIELD];
static int  vault_count;
static error *vault_err;

/* argv is an FFI pointer: copy the slot with memcpy so --strict can prove it. */
static char *vault_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static void vault_refuse(const char *reason) {
    if (vault_err != NULL)
        error_fail(vault_err, CPLUS_EXIT_UNSUPPORTED, reason);
    print("VAULT error", reason);
}

static void vault_refuse_range(const char *reason) {
    if (vault_err != NULL)
        error_fail(vault_err, CPLUS_EXIT_BOUNDS, reason);
    print("VAULT error", reason);
}

static int vault_find(const char *service) {
    int i;
    for (i = 0; i < vault_count; i++) {
        if (strcmp(vault_names[i], service) == 0)
            return i;
    }
    return -1;
}

/* Checked copy: return 0 when empty, over the 256-byte cap, or copy fails. */
static int vault_copy(const char *src, char *dst, size_t cap) {
    string text = string(src);
    size_t n;
    int ok = 0;
    if (text.valid()) {
        n = text.size();
        if (n > 0 && n <= VAULT_LIMIT && n + 1 <= cap) {
            if (text.substr(0, n, dst, cap) == 0)
                ok = 1;
        }
    }
    text.drop();
    return ok;
}

static int vault_add(const char *service, const char *password) {
    char name[VAULT_FIELD];
    char secret[VAULT_FIELD];
    int rc_name;
    int rc_secret;

    /* Length is tested before copying: string::substr truncates silently. */
    {
        string a = string(service);
        string b = string(password);
        int too_long = (a.valid() && a.size() > VAULT_LIMIT) ||
                       (b.valid() && b.size() > VAULT_LIMIT);
        a.drop();
        b.drop();
        if (too_long) {
            if (strlen(service) > VAULT_LIMIT) {
                vault_refuse_range("name-too-long");
            } else {
                vault_refuse_range("password-too-long");
            }
            return 1;
        }
    }
    rc_name = vault_copy(service, name, sizeof(name));
    if (!rc_name) {
        vault_refuse("empty-name");
        return 1;
    }
    rc_secret = vault_copy(password, secret, sizeof(secret));
    if (!rc_secret) {
        vault_refuse("empty-password");
        return 1;
    }
    if (vault_find(name) >= 0) {
        vault_refuse("duplicate");
        return 1;
    }
    if (vault_count >= VAULT_MAX) {
        vault_refuse_range("full");
        return 1;
    }
    __builtin_memcpy(vault_names[vault_count], name, VAULT_FIELD);
    __builtin_memcpy(vault_secrets[vault_count], secret, VAULT_FIELD);
    vault_count++;
    print("VAULT add", name);
    return 0;
}

static int vault_get(const char *service) {
    int pos = vault_find(service);
    if (pos < 0) {
        vault_refuse("missing-key");
        return 1;
    }
    /* The only place a secret is ever emitted. */
    print("VAULT get", service, vault_secrets[pos]);
    return 0;
}

static void vault_list(void) {
    int i;
    print("VAULT list", vault_count);
    for (i = 0; i < vault_count; i++)
        print("VAULT entry", i, vault_names[i]); /* never the secret */
}

static int vault_delete(const char *service) {
    int pos = vault_find(service);
    int i;
    if (pos < 0) {
        vault_refuse("missing-key");
        return 1;
    }
    for (i = pos; i + 1 < vault_count; i++) {
        __builtin_memcpy(vault_names[i], vault_names[i + 1], VAULT_FIELD);
        __builtin_memcpy(vault_secrets[i], vault_secrets[i + 1], VAULT_FIELD);
    }
    vault_count--;
    print("VAULT delete", service);
    return 0;
}

static int vault_execute(const char *cmd, const char *a1, const char *a2) {
    if (cmd == NULL) {
        vault_refuse("usage");
        return 2;
    }
    if (strcmp(cmd, "add") == 0) {
        if (a1 == NULL || a2 == NULL) {
            vault_refuse("bad-args");
            return 1;
        }
        return vault_add(a1, a2);
    }
    if (strcmp(cmd, "get") == 0 || strcmp(cmd, "delete") == 0) {
        int is_delete = strcmp(cmd, "delete") == 0;
        if (a1 == NULL || a2 != NULL) {
            vault_refuse("bad-args");
            return 1;
        }
        return is_delete ? vault_delete(a1) : vault_get(a1);
    }
    if (strcmp(cmd, "list") == 0) {
        if (a1 != NULL) {
            vault_refuse("bad-args");
            return 1;
        }
        vault_list();
        return 0;
    }
    vault_refuse("unknown-command");
    return 1;
}

int main(int argc, char **argv) {
    error ferr = error();
    int i = 1;
    int rc = 0;

    vault_err = &ferr;
    if (argc < 2) {
        vault_refuse("usage");
        return 2;
    }
    while (i < argc) {
        const char *cmd = vault_arg(argv, (size_t)i);
        const char *a1 = NULL;
        const char *a2 = NULL;
        i++;
        if (i < argc && strcmp(vault_arg(argv, (size_t)i), "--") != 0) {
            a1 = vault_arg(argv, (size_t)i);
            i++;
        }
        if (i < argc && strcmp(vault_arg(argv, (size_t)i), "--") != 0) {
            a2 = vault_arg(argv, (size_t)i);
            i++;
        }
        while (i < argc && strcmp(vault_arg(argv, (size_t)i), "--") != 0) {
            vault_refuse("bad-args");
            rc = 1;
            i++;
        }
        if (i < argc)
            i++;
        if (vault_execute(cmd, a1, a2) != 0)
            rc = 1;
    }
    return rc;
}
