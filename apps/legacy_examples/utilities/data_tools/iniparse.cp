/* iniparse.cp -- fixed INI fixture parser with strict refusals.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/utilities/data_tools/iniparse.cp \
 *     --strict --sanitize=none -I. \
 *     -o apps/legacy_examples/utilities/data_tools/.run/iniparse
 *
 * Usage:
 *   iniparse <ini-path> get <section> <key>
 *   iniparse <ini-path> sections
 *   iniparse <ini-path> keys <section>
 *
 * The fixture is a bounded INI document: `[section]` headers, `key=value`
 * entries, blank lines and `#` / `;` comments. Every read goes through the
 * checked file::read_line API. Over-long lines, oversized section/key/value
 * names, malformed lines, keys outside a section, duplicate keys, table
 * overflow and missing lookups are ALL refused with a stable token. All
 * tables are fixed-size; no dynamic allocation is used. Inputs and output are
 * deterministic: fixed argv, fixed file bytes, no clock.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/file.hp"

#define IP_LINE      256
#define IP_PATH      512
#define IP_CMD        32
#define IP_NAME       32   /* max section / key payload bytes */
#define IP_VALUE      64   /* max value payload bytes */
#define IP_SECTIONS   16
#define IP_KEYS       16

static char sections[IP_SECTIONS][IP_NAME + 1];
static char keys[IP_SECTIONS][IP_KEYS][IP_NAME + 1];
static char values[IP_SECTIONS][IP_KEYS][IP_VALUE + 1];
static int section_count = 0;
static int key_count[IP_SECTIONS];

/* argv is an FFI pointer: copy the slot with memcpy so --strict can prove it. */
static char *ip_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static void ip_refuse(error *err, const char *code, cplus_exit_code status) {
    error_fail(err, status, code);
    print("INI error", code);
}

static int ip_copy(const char *src, char *dst, size_t cap, size_t *out_len) {
    string s = string(src);
    size_t n;
    int ok = 0;
    if (s.valid()) {
        n = s.size();
        if (n > 0 && n + 1 <= cap) {
            if (s.substr(0, n, dst, cap) == 0) {
                *out_len = n;
                ok = 1;
            }
        }
    }
    s.drop();
    return ok;
}

static int ip_find_section(const char *name) {
    int i;
    for (i = 0; i < section_count; i++) {
        if (strcmp(sections[i], name) == 0)
            return i;
    }
    return -1;
}

static int ip_find_key(int section, const char *key) {
    int i;
    for (i = 0; i < key_count[section]; i++) {
        if (strcmp(keys[section][i], key) == 0)
            return i;
    }
    return -1;
}

/* Parse the whole fixture into the fixed tables. Returns 1 on success, 0 on
 * a refusal (with err set and a token already printed). */
static int ip_load(const char *path, error *err) {
    file stream = file(path, "r");
    char line[IP_LINE];
    char name[IP_NAME + 1];
    char key[IP_NAME + 1];
    char val[IP_VALUE + 1];
    size_t n;
    size_t i;
    size_t eq;
    size_t name_len;
    int rc;
    int current = -1;
    int ok = 1;

    if (!stream.is_open()) {
        ip_refuse(err, "cannot-open", CPLUS_EXIT_IO);
        return 0;
    }

    for (;;) {
        rc = stream.read_line(line, sizeof(line), err);
        if (rc == STDLIB_EOF)
            break;
        if (rc != STDLIB_OK) {
            ip_refuse(err,
                      err->code() == (int)CPLUS_EXIT_BOUNDS
                          ? "line-too-long" : "read-error",
                      CPLUS_EXIT_BOUNDS);
            ok = 0;
            break;
        }
        n = strlen(line);
        if (n == 0 || line[0] == '#' || line[0] == ';')
            continue;

        if (line[0] == '[') {
            /* section header: [name] and nothing else */
            if (n < 3 || line[n - 1] != ']') {
                ip_refuse(err, "bad-line", CPLUS_EXIT_UNSUPPORTED);
                ok = 0;
                break;
            }
            name_len = n - 2;
            if (name_len == 0 || name_len > IP_NAME) {
                ip_refuse(err, "name-too-long", CPLUS_EXIT_BOUNDS);
                ok = 0;
                break;
            }
            for (i = 0; i < name_len; i++) {
                if (line[i + 1] == '[' || line[i + 1] == ']' ||
                    line[i + 1] == '=') {
                    ip_refuse(err, "bad-line", CPLUS_EXIT_UNSUPPORTED);
                    ok = 0;
                    break;
                }
                name[i] = line[i + 1];
            }
            if (!ok)
                break;
            name[name_len] = '\0';
            if (ip_find_section(name) >= 0) {
                ip_refuse(err, "duplicate-section", CPLUS_EXIT_UNSUPPORTED);
                ok = 0;
                break;
            }
            if (section_count >= IP_SECTIONS) {
                ip_refuse(err, "too-many-sections", CPLUS_EXIT_BOUNDS);
                ok = 0;
                break;
            }
            for (i = 0; i <= name_len; i++)
                sections[section_count][i] = name[i];
            current = section_count;
            section_count++;
            continue;
        }

        /* key=value entry */
        eq = 0;
        for (i = 0; i < n; i++) {
            if (line[i] == '=') {
                eq = i;
                break;
            }
            eq = 0;
        }
        if (eq == 0 || eq > IP_NAME || n - eq - 1 > IP_VALUE) {
            ip_refuse(err, "bad-line", CPLUS_EXIT_UNSUPPORTED);
            ok = 0;
            break;
        }
        for (i = 0; i < eq; i++) {
            if (line[i] == '[' || line[i] == ']') {
                ip_refuse(err, "bad-line", CPLUS_EXIT_UNSUPPORTED);
                ok = 0;
                break;
            }
            key[i] = line[i];
        }
        if (!ok)
            break;
        key[eq] = '\0';
        for (i = 0; i < n - eq - 1; i++)
            val[i] = line[eq + 1 + i];
        val[n - eq - 1] = '\0';
        if (current < 0) {
            ip_refuse(err, "no-section", CPLUS_EXIT_UNSUPPORTED);
            ok = 0;
            break;
        }
        if (ip_find_key(current, key) >= 0) {
            ip_refuse(err, "duplicate-key", CPLUS_EXIT_UNSUPPORTED);
            ok = 0;
            break;
        }
        if (key_count[current] >= IP_KEYS) {
            ip_refuse(err, "too-many-keys", CPLUS_EXIT_BOUNDS);
            ok = 0;
            break;
        }
        i = key_count[current];
        for (size_t k = 0; k <= eq; k++)
            keys[current][i][k] = key[k];
        for (size_t k = 0; k <= n - eq - 1; k++)
            values[current][i][k] = val[k];
        key_count[current]++;
    }

    if (stream.close(err) != STDLIB_OK) {
        stream.drop();
        ip_refuse(err, "close-error", CPLUS_EXIT_IO);
        return 0;
    }
    stream.drop();
    return ok;
}

int main(int argc, char **argv) {
    error err = error();
    char path[IP_PATH];
    char cmd[IP_CMD];
    char sec[IP_NAME + 1];
    char key[IP_NAME + 1];
    size_t path_len = 0;
    size_t cmd_len = 0;
    size_t sec_len = 0;
    size_t key_len = 0;
    int si;
    int ki;
    int i;

    if (argc < 3) {
        ip_refuse(&err, "usage", CPLUS_EXIT_UNSUPPORTED);
        return 2;
    }
    if (!ip_copy(ip_arg(argv, 1), path, sizeof(path), &path_len) ||
        !ip_copy(ip_arg(argv, 2), cmd, sizeof(cmd), &cmd_len)) {
        ip_refuse(&err, "bad-arg", CPLUS_EXIT_BOUNDS);
        return 1;
    }

    if (strcmp(cmd, "get") == 0) {
        if (argc != 5) {
            ip_refuse(&err, "bad-args", CPLUS_EXIT_UNSUPPORTED);
            return 1;
        }
        if (!ip_copy(ip_arg(argv, 3), sec, sizeof(sec), &sec_len) ||
            !ip_copy(ip_arg(argv, 4), key, sizeof(key), &key_len)) {
            ip_refuse(&err, "name-too-long", CPLUS_EXIT_BOUNDS);
            return 1;
        }
    } else if (strcmp(cmd, "keys") == 0) {
        if (argc != 4) {
            ip_refuse(&err, "bad-args", CPLUS_EXIT_UNSUPPORTED);
            return 1;
        }
        if (!ip_copy(ip_arg(argv, 3), sec, sizeof(sec), &sec_len)) {
            ip_refuse(&err, "name-too-long", CPLUS_EXIT_BOUNDS);
            return 1;
        }
    } else if (strcmp(cmd, "sections") == 0) {
        if (argc != 3) {
            ip_refuse(&err, "bad-args", CPLUS_EXIT_UNSUPPORTED);
            return 1;
        }
    } else {
        ip_refuse(&err, "bad-command", CPLUS_EXIT_UNSUPPORTED);
        return 1;
    }

    if (!ip_load(path, &err))
        return 1;

    if (strcmp(cmd, "sections") == 0) {
        print("INI sections", section_count);
        for (i = 0; i < section_count; i++)
            print("INI section", i, sections[i]);
        return 0;
    }
    if (strcmp(cmd, "keys") == 0) {
        si = ip_find_section(sec);
        if (si < 0) {
            ip_refuse(&err, "no-section", CPLUS_EXIT_UNSUPPORTED);
            return 1;
        }
        print("INI keys", sec, key_count[si]);
        for (i = 0; i < key_count[si]; i++)
            print("INI key", i, keys[si][i]);
        return 0;
    }

    /* get */
    si = ip_find_section(sec);
    if (si < 0) {
        ip_refuse(&err, "no-section", CPLUS_EXIT_UNSUPPORTED);
        return 1;
    }
    ki = ip_find_key(si, key);
    if (ki < 0) {
        ip_refuse(&err, "no-key", CPLUS_EXIT_UNSUPPORTED);
        return 1;
    }
    print("INI value", sec, key, values[si][ki]);
    return 0;
}
