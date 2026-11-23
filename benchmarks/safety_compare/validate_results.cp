/* C+ validator for benchmark evidence files (CSV or JSON).
 *
 * This is a self-contained C+ program: it reads evidence with stdlib/file.hp
 * and parses it with simple string scanning (no Python, no external parsers).
 * It verifies four properties of every evidence file passed on the command
 * line, then prints a single machine-readable summary line:
 *
 *     BENCH_VALID <checks> <failures>
 *
 * Properties checked (see VALIDATOR_NOTES.md for the exact schema):
 *   1. required columns / fields exist   -> workload, language, runs
 *   2. checksums agree across languages  -> per workload, when >=2 languages
 *                                            report a non-empty checksum
 *   3. run counts >= 7                   -> runs must be an integer >= 7
 *   4. numeric values are finite         -> every numeric field parses fully
 *                                            and isfinite() (no NaN / Inf)
 *
 * Exit status is 0 only when failures == 0.
 *
 * Build (from the repository root):
 *   timeout 30 build/c+ build benchmarks/safety_compare/validate_results.cp \
 *       --strict --sanitize=none -o build/validate_results
 */

#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

#define LINE_CAP 2048
#define FIELD_CAP 256
#define MAX_FIELDS 48
#define MAX_RECORDS 512
#define MAX_NUMERIC 24
#define MAX_GROUPS 256
#define MAX_LANGS 16

typedef struct {
    char workload[FIELD_CAP];
    char language[FIELD_CAP];
    char checksum[FIELD_CAP];
    char runs_text[FIELD_CAP];
    int has_workload;
    int has_language;
    int has_checksum;
    int has_runs;
    int numeric_count;
    char numeric_name[MAX_NUMERIC][FIELD_CAP];
    double numeric_value[MAX_NUMERIC];
} Record;

static Record g_records[MAX_RECORDS];
static int g_record_count = 0;

static char g_columns[MAX_FIELDS][FIELD_CAP];
static int g_column_count = 0;

static int g_checks = 0;
static int g_failures = 0;

/* ------------------------------------------------------------------ */
/* Byte helpers: strict mode cannot prove the extent of a pointer       */
/* parameter, so byte loads/stores go through __builtin_memcpy (the      */
/* pattern used by stdlib/file.hp and the hostile-input probes).         */
/* ------------------------------------------------------------------ */

static char byte_at(const char *p, size_t i) {
    char c;
    __builtin_memcpy(&c, p + i, 1);
    return c;
}

static void byte_put(char *p, size_t i, char c) {
    __builtin_memcpy(p + i, &c, 1);
}

static char *arg_at(char **argv, size_t i) {
    char *value;
    __builtin_memcpy(&value, argv + i, sizeof(value));
    return value;
}

/* ------------------------------------------------------------------ */
/* Check accounting                                                    */
/* ------------------------------------------------------------------ */

static void add_check(const char *tag, int ok) {
    g_checks++;
    if (!ok) {
        g_failures++;
        printf("BENCH_FAIL %s\n", tag);
    }
}

/* ------------------------------------------------------------------ */
/* Small string helpers                                                */
/* ------------------------------------------------------------------ */

static void copy_trimmed(const char *src, size_t n, char *dst, size_t cap) {
    size_t start = 0;
    size_t end = n;
    size_t k = 0;
    while (start < end && isspace((unsigned char)byte_at(src, start)))
        start++;
    while (end > start && isspace((unsigned char)byte_at(src, end - 1)))
        end--;
    while (start < end && k + 1 < cap) {
        byte_put(dst, k, byte_at(src, start));
        k++;
        start++;
    }
    byte_put(dst, k, '\0');
}

/* Split one delimited line into fields. Returns field count, or -1 on
 * overflow (more fields than the caller's capacity). */
static int split_fields(const char *line, char sep,
                        char out[MAX_FIELDS][FIELD_CAP]) {
    size_t len = strlen(line);
    size_t i = 0;
    size_t start = 0;
    int n = 0;
    while (i <= len) {
        if (i == len || byte_at(line, i) == sep) {
            if (n >= MAX_FIELDS)
                return -1;
            copy_trimmed(line + start, i - start, out[n], FIELD_CAP);
            n++;
            start = i + 1;
        }
        i++;
    }
    return n;
}

static int find_column(const char *name) {
    int c;
    for (c = 0; c < g_column_count; c++)
        if (strcmp(g_columns[c], name) == 0)
            return c;
    return -1;
}

static Record *new_record(void) {
    Record *r;
    if (g_record_count >= MAX_RECORDS)
        return NULL;
    r = &g_records[g_record_count++];
    memset(r, 0, sizeof(*r));
    return r;
}

static void record_set(Record *r, const char *key, const char *value,
                       int quoted) {
    if (strcmp(key, "workload") == 0) {
        snprintf(r->workload, FIELD_CAP, "%s", value);
        r->has_workload = byte_at(value, 0) != '\0';
    } else if (strcmp(key, "language") == 0) {
        snprintf(r->language, FIELD_CAP, "%s", value);
        r->has_language = byte_at(value, 0) != '\0';
    } else if (strcmp(key, "checksum") == 0) {
        snprintf(r->checksum, FIELD_CAP, "%s", value);
        r->has_checksum = byte_at(value, 0) != '\0';
    } else if (strcmp(key, "runs") == 0 ||
               (strcmp(key, "runs_ok") == 0 && !r->has_runs) ||
               (strcmp(key, "runs_total") == 0 && !r->has_runs)) {
        snprintf(r->runs_text, FIELD_CAP, "%s", value);
        r->has_runs = byte_at(value, 0) != '\0';
    }
    if (!quoted && byte_at(value, 0) != '\0') {
        char *end = NULL;
        double d = strtod(value, &end);
        if (end != value && *end == '\0' && r->numeric_count < MAX_NUMERIC) {
            snprintf(r->numeric_name[r->numeric_count], FIELD_CAP, "%s", key);
            r->numeric_value[r->numeric_count] = d;
            r->numeric_count++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Evidence file loading (stdlib/file.hp, no Python)                   */
/* ------------------------------------------------------------------ */

static long load_evidence(const char *path, char *buf, size_t cap) {
    error load_err = error();
    size_t total = 0;
    long result = -1;
    file handle = file(path, "rb");
    if (!handle.is_open())
        return -1;
    for (;;) {
        long before_pos;
        long after_pos;
        size_t remain;
        int read_rc;
        if (total + 1 >= cap)
            break;
        before_pos = ftell(handle.native());
        if (before_pos < 0)
            break;
        remain = cap - total - 1;
        read_rc = handle.read(buf + total, remain, &load_err);
        after_pos = ftell(handle.native());
        if (after_pos < before_pos)
            break;
        total += (size_t)(after_pos - before_pos);
        if (read_rc == STDLIB_EOF || after_pos == before_pos)
            break;
        if (read_rc != STDLIB_OK)
            break;
    }
    if (total + 1 < cap) {
        byte_put(buf, total, '\0');
        result = (long)total;
    }
    handle.drop();
    return result;
}

/* ------------------------------------------------------------------ */
/* CSV parsing                                                         */
/* ------------------------------------------------------------------ */

static int parse_columns(const char *text) {
    char fields[MAX_FIELDS][FIELD_CAP];
    int n;
    g_column_count = 0;
    n = split_fields(text, ',', fields);
    if (n <= 0 || n > MAX_FIELDS)
        return 0;
    for (int i = 0; i < n; i++) {
        snprintf(g_columns[g_column_count], FIELD_CAP, "%s", fields[i]);
        g_column_count++;
    }
    return 1;
}

static int parse_csv(const char *buf, size_t len) {
    size_t pos = 0;
    int header_seen = 0;
    int rows = 0;

    while (pos < len) {
        char line[LINE_CAP];
        size_t n = 0;
        size_t start = pos;
        size_t end = pos;
        size_t k;
        while (end < len && byte_at(buf, end) != '\n' &&
               byte_at(buf, end) != '\r')
            end++;
        while (start < end && isspace((unsigned char)byte_at(buf, start)))
            start++;
        n = end - start;
        if (n >= LINE_CAP)
            n = LINE_CAP - 1;
        for (k = 0; k < n; k++)
            line[k] = byte_at(buf, start + k);
        line[n] = '\0';
        pos = end;
        while (pos < len && (byte_at(buf, pos) == '\n' ||
                             byte_at(buf, pos) == '\r'))
            pos++;

        if (line[0] == '\0')
            continue;
        if (line[0] == '#') {
            const char *marker = strstr(line, "columns:");
            if (marker != NULL && !header_seen) {
                if (!parse_columns(marker + 8))
                    return 0;
                header_seen = 1;
            }
            continue;
        }
        if (!header_seen) {
            /* Fallback: first non-comment line is the column header. */
            if (!parse_columns(line))
                return 0;
            header_seen = 1;
            continue;
        }

        {
            char fields[MAX_FIELDS][FIELD_CAP];
            int nfields = split_fields(line, ',', fields);
            char tag[FIELD_CAP + 64];
            Record *r;
            int c;
            rows++;
            snprintf(tag, sizeof(tag), "csv.row%d.field_count", rows);
            add_check(tag, nfields == g_column_count);
            if (nfields <= 0)
                continue;
            r = new_record();
            if (r == NULL) {
                add_check("csv.record_capacity", 0);
                continue;
            }
            for (c = 0; c < nfields && c < g_column_count; c++) {
                const char *col = g_columns[c];
                const char *val = fields[c];
                if (strcmp(col, "workload") == 0) {
                    snprintf(r->workload, FIELD_CAP, "%s", val);
                    r->has_workload = byte_at(val, 0) != '\0';
                } else if (strcmp(col, "language") == 0) {
                    snprintf(r->language, FIELD_CAP, "%s", val);
                    r->has_language = byte_at(val, 0) != '\0';
                } else if (strcmp(col, "checksum") == 0) {
                    snprintf(r->checksum, FIELD_CAP, "%s", val);
                    r->has_checksum = byte_at(val, 0) != '\0';
                } else if (strcmp(col, "runs") == 0) {
                    snprintf(r->runs_text, FIELD_CAP, "%s", val);
                    r->has_runs = byte_at(val, 0) != '\0';
                } else {
                    char *end = NULL;
                    double d = strtod(val, &end);
                    if (end != val && *end == '\0' &&
                        r->numeric_count < MAX_NUMERIC) {
                        snprintf(r->numeric_name[r->numeric_count], FIELD_CAP,
                                 "%s", col);
                        r->numeric_value[r->numeric_count] = d;
                        r->numeric_count++;
                    }
                }
            }
        }
    }

    add_check("csv.header_present", header_seen);
    add_check("csv.column.workload", find_column("workload") >= 0);
    add_check("csv.column.language", find_column("language") >= 0);
    add_check("csv.column.runs", find_column("runs") >= 0);
    add_check("csv.rows_present", rows > 0 ? 1 : 0);
    return 1;
}

/* ------------------------------------------------------------------ */
/* JSON parsing (narrow, simple scanner for the documented schema)     */
/* ------------------------------------------------------------------ */

static void skip_ws(const char *s, size_t len, size_t *i) {
    while (*i < len && isspace((unsigned char)byte_at(s, *i)))
        (*i)++;
}

static int read_json_string(const char *s, size_t len, size_t *i,
                            char *out, size_t cap) {
    size_t n = 0;
    if (*i >= len || byte_at(s, *i) != '"')
        return 0;
    (*i)++;
    while (*i < len) {
        char c = byte_at(s, *i);
        if (c == '"') {
            (*i)++;
            byte_put(out, n, '\0');
            return 1;
        }
        if (c == '\\' && *i + 1 < len) {
            (*i)++;
            c = byte_at(s, *i);
            if (c == 'n')
                c = '\n';
            else if (c == 't')
                c = '\t';
            else if (c == 'r')
                c = '\r';
        }
        if (n + 1 < cap) {
            byte_put(out, n, c);
            n++;
        }
        (*i)++;
    }
    byte_put(out, n, '\0');
    return 0;
}

static int read_json_token(const char *s, size_t len, size_t *i,
                           char *out, size_t cap) {
    size_t n = 0;
    while (*i < len && (unsigned char)byte_at(s, *i) > ' ' &&
           byte_at(s, *i) != ',' && byte_at(s, *i) != '}' &&
           byte_at(s, *i) != ']') {
        if (n + 1 < cap) {
            byte_put(out, n, byte_at(s, *i));
            n++;
        }
        (*i)++;
    }
    byte_put(out, n, '\0');
    return n > 0;
}

/* Skip a nested object/array value (e.g. "artifacts": {...}) so the simple
 * scanner stays at the enclosing record's level. Strings are tracked so
 * braces and commas inside path values do not end the nested value early. */
static int skip_json_value(const char *s, size_t len, size_t *i) {
    char scratch[FIELD_CAP];
    int depth = 0;
    int in_string = 0;
    int escaped = 0;
    if (*i >= len)
        return 0;
    if (byte_at(s, *i) == '"')
        return read_json_string(s, len, i, scratch, FIELD_CAP) ? 1 : 0;
    if (byte_at(s, *i) != '{' && byte_at(s, *i) != '[')
        return read_json_token(s, len, i, scratch, FIELD_CAP) ? 1 : 0;
    while (*i < len) {
        char c = byte_at(s, *i);
        if (in_string) {
            if (escaped)
                escaped = 0;
            else if (c == '\\')
                escaped = 1;
            else if (c == '"')
                in_string = 0;
            (*i)++;
            continue;
        }
        if (c == '"') {
            in_string = 1;
            (*i)++;
            continue;
        }
        (*i)++;
        if (c == '{' || c == '[')
            depth++;
        else if (c == '}' || c == ']') {
            depth--;
            if (depth == 0)
                return 1;
        }
    }
    return 0;
}

static size_t find_literal(const char *s, size_t len, const char *needle) {
    size_t n = strlen(needle);
    size_t i;
    if (n == 0 || len < n)
        return len;
    for (i = 0; i + n <= len; i++)
        if (strncmp(s + i, needle, n) == 0)
            return i;
    return len;
}

static int parse_json(const char *buf, size_t len) {
    size_t i = 0;
    size_t p;
    int records = 0;

    skip_ws(buf, len, &i);
    add_check("json.root_object", i < len && byte_at(buf, i) == '{');
    /* Accept either "records": [...] or "results": [...] as the record array. */
    p = find_literal(buf, len, "\"records\"");
    if (p >= len)
        p = find_literal(buf, len, "\"results\"");
    if (p >= len) {
        add_check("json.array_key", 0);
        add_check("json.records_present", 0);
        return 0;
    }
    add_check("json.array_key", 1);
    i = p + 9;
    skip_ws(buf, len, &i);
    if (i >= len || byte_at(buf, i) != ':') {
        add_check("json.records_array", 0);
        return 0;
    }
    i++;
    skip_ws(buf, len, &i);
    if (i >= len || byte_at(buf, i) != '[') {
        add_check("json.records_array", 0);
        return 0;
    }
    add_check("json.records_array", 1);
    i++;

    {
        int first_record = 1;
        for (;;) {
            Record *r;
            skip_ws(buf, len, &i);
            if (i < len && byte_at(buf, i) == ']')
                break;
            if (!first_record) {
                if (i >= len || byte_at(buf, i) != ',') {
                    add_check("json.record_separator", 0);
                    return 0;
                }
                i++;
                skip_ws(buf, len, &i);
            }
            first_record = 0;
            if (i >= len || byte_at(buf, i) != '{') {
                add_check("json.record_object", 0);
                return 0;
            }
            i++;
            r = new_record();
            if (r == NULL) {
                add_check("json.record_capacity", 0);
                return 0;
            }
            records++;
            for (;;) {
                char key[FIELD_CAP];
                char value[FIELD_CAP];
                int quoted = 0;
                skip_ws(buf, len, &i);
                if (i < len && byte_at(buf, i) == '}') {
                    i++;
                    break;
                }
                if (i >= len ||
                    !read_json_string(buf, len, &i, key, FIELD_CAP)) {
                    add_check("json.record_key", 0);
                    return 0;
                }
                skip_ws(buf, len, &i);
                if (i >= len || byte_at(buf, i) != ':') {
                    add_check("json.record_colon", 0);
                    return 0;
                }
                i++;
                skip_ws(buf, len, &i);
                if (i < len && byte_at(buf, i) == '"') {
                    quoted = 1;
                    if (!read_json_string(buf, len, &i, value, FIELD_CAP)) {
                        add_check("json.record_string", 0);
                        return 0;
                    }
                } else if (!read_json_token(buf, len, &i, value, FIELD_CAP)) {
                    add_check("json.record_value", 0);
                    return 0;
                }
                record_set(r, key, value, quoted);
                skip_ws(buf, len, &i);
                if (i < len && byte_at(buf, i) == ',') {
                    i++;
                    continue;
                }
                if (i < len && byte_at(buf, i) == '}') {
                    i++;
                    break;
                }
                add_check("json.record_separator", 0);
                return 0;
            }
        }
    }
    add_check("json.records_present", records > 0 ? 1 : 0);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Common record validation                                            */
/* ------------------------------------------------------------------ */

static void validate_records(void) {
    int i;
    for (i = 0; i < g_record_count; i++) {
        Record *r = &g_records[i];
        char tag[FIELD_CAP + 64];

        snprintf(tag, sizeof(tag), "record%d.workload_present", i + 1);
        add_check(tag, r->has_workload && byte_at(r->workload, 0) != '\0');
        snprintf(tag, sizeof(tag), "record%d.language_present", i + 1);
        add_check(tag, r->has_language && byte_at(r->language, 0) != '\0');
        snprintf(tag, sizeof(tag), "record%d.runs_present", i + 1);
        add_check(tag, r->has_runs && byte_at(r->runs_text, 0) != '\0');

        if (r->has_runs) {
            char *end = NULL;
            long runs = strtol(r->runs_text, &end, 10);
            snprintf(tag, sizeof(tag), "record%d.runs_integer", i + 1);
            add_check(tag, end != r->runs_text && *end == '\0' && runs > 0);
            snprintf(tag, sizeof(tag), "record%d.runs_ge_7", i + 1);
            add_check(tag, end != r->runs_text && *end == '\0' && runs >= 7);
        }

        for (int k = 0; k < r->numeric_count; k++) {
            snprintf(tag, sizeof(tag), "record%d.%s.finite", i + 1,
                     r->numeric_name[k]);
            add_check(tag, isfinite(r->numeric_value[k]) ? 1 : 0);
        }
    }
}

/* One check per workload that has >=2 languages reporting a checksum. */
static void validate_checksums(void) {
    char groups[MAX_GROUPS][FIELD_CAP];
    char langs[MAX_LANGS][FIELD_CAP];
    int ngroups = 0;
    int i, k, m;

    for (i = 0; i < g_record_count; i++) {
        int found = 0;
        if (!g_records[i].has_workload || byte_at(g_records[i].workload, 0) == '\0')
            continue;
        for (k = 0; k < ngroups; k++) {
            if (strcmp(groups[k], g_records[i].workload) == 0) {
                found = 1;
                break;
            }
        }
        if (!found && ngroups < MAX_GROUPS) {
            snprintf(groups[ngroups], FIELD_CAP, "%s",
                     g_records[i].workload);
            ngroups++;
        }
    }

    for (k = 0; k < ngroups; k++) {
        int idx[MAX_RECORDS];
        int n = 0;
        int nlang = 0;
        char tag[FIELD_CAP + 64];
        for (i = 0; i < g_record_count; i++) {
            Record *r = &g_records[i];
            if (!r->has_workload || strcmp(r->workload, groups[k]) != 0)
                continue;
            if (!r->has_checksum || byte_at(r->checksum, 0) == '\0')
                continue;
            if (n < MAX_RECORDS)
                idx[n++] = i;
            if (r->has_language) {
                int have = 0;
                for (m = 0; m < nlang; m++) {
                    if (strcmp(langs[m], r->language) == 0) {
                        have = 1;
                        break;
                    }
                }
                if (!have && nlang < MAX_LANGS) {
                    snprintf(langs[nlang], FIELD_CAP, "%s", r->language);
                    nlang++;
                }
            }
        }
        if (n >= 2 && nlang >= 2) {
            int ok = 1;
            for (m = 1; m < n; m++) {
                if (strcmp(g_records[idx[0]].checksum,
                           g_records[idx[m]].checksum) != 0) {
                    ok = 0;
                    break;
                }
            }
            snprintf(tag, sizeof(tag), "checksum_match:%s", groups[k]);
            add_check(tag, ok);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

static void validate_file(const char *path) {
    static char buf[LINE_CAP * 8];
    long count;
    size_t len;
    int is_json = 0;
    size_t plen = strlen(path);

    g_record_count = 0;
    g_column_count = 0;

    if (plen >= 5 && strcmp(path + plen - 5, ".json") == 0)
        is_json = 1;
    else if (plen >= 4 && strcmp(path + plen - 4, ".csv") == 0)
        is_json = 0;
    else
        is_json = plen > 0 && byte_at(path, 0) == '{';

    count = load_evidence(path, buf, sizeof(buf));
    {
        char tag[FIELD_CAP + 64];
        snprintf(tag, sizeof(tag), "file_open:%s", path);
        add_check(tag, count >= 0 ? 1 : 0);
    }
    if (count < 0)
        return;
    len = (size_t)count;
    add_check("evidence_nonempty", len > 0 ? 1 : 0);
    if (len == 0)
        return;

    if (is_json) {
        add_check("format_json", 1);
        if (!parse_json(buf, len))
            return;
    } else {
        add_check("format_csv", 1);
        if (!parse_csv(buf, len))
            return;
    }
    validate_records();
    validate_checksums();
}

int main(int argc, char **argv) {
    int f;
    if (argc < 2) {
        char *prog = arg_at(argv, 0);
        printf("usage: %s <evidence.csv|evidence.json> [more ...]\n", prog);
        add_check("arguments_present", 0);
        print("BENCH_VALID", g_checks, g_failures);
        return 1;
    }
    for (f = 1; f < argc; f++)
        validate_file(arg_at(argv, (size_t)f));
    print("BENCH_VALID", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
