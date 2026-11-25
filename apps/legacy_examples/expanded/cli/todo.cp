/* todo.cp -- deterministic in-memory task list CLI built on the C+ stdlib.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/cli/todo.cp --strict --sanitize=none -I. \
 *       -o examples/expanded/cli/.run/todo
 *
 * Commands (all arguments are fixed; no prompts, no clock, no shell):
 *   todo add <id> <title>
 *   todo list
 *   todo done <id>
 *   todo remove <id>
 *   todo save <path>
 *   todo load <path>
 *
 * Several commands may be chained in one process with a "--" separator:
 *   todo add 1 alpha -- add 2 beta -- done 1 -- list -- save <path> -- load <path>
 * This keeps the state in one in-memory vector and makes a whole scenario
 * deterministic in a single run.
 *
 * State lives in a stdlib `vector` (ids + done flags) and a bounded title
 * table. Titles are copied through `string::substr` (checked) and file
 * records are parsed through `string::field` (checked). Every vector read
 * uses `at_checked`; every file operation reports through `error *`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"
#include "stdlib/file.hp"

#define TD_MAX_TASKS 64
#define TD_TITLE_CAP 80
#define TD_LINE_CAP  256

static char td_titles[TD_MAX_TASKS][TD_TITLE_CAP];
static int  td_states[TD_MAX_TASKS];

/* argv is an FFI pointer: copy the slot with memcpy so the strict checker
 * can prove the read, then hand the resulting pointer to libc/string APIs. */
static char *td_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static int td_parse_int(const char *text, int *out) {
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

static void td_error(const char *what) {
    print("TODO error", what);
}

/* Find the position of `id`; -1 when absent. All vector reads are checked. */
static int td_find(vector *ids, int id) {
    int i;
    int n = (int)ids->size();
    for (i = 0; i < n; i++) {
        int cur = 0;
        if (ids->at_checked((size_t)i, &cur) != 0)
            return -1;
        if (cur == id)
            return i;
    }
    return -1;
}

/* Copy a title into bounded storage through the checked string API. */
static int td_set_title(int index, const char *text) {
    string tmp = string(text);
    char buf[TD_TITLE_CAP];
    if (!tmp.valid() || tmp.size() + 1 > sizeof(buf)) {
        tmp.drop();
        return 0;
    }
    if (tmp.substr(0, tmp.size(), buf, sizeof(buf)) != 0) {
        tmp.drop();
        return 0;
    }
    __builtin_memcpy(td_titles[index], buf, sizeof(buf));
    tmp.drop();
    return 1;
}

static void td_list(vector *ids) {
    int i;
    int n = (int)ids->size();
    for (i = 0; i < n; i++) {
        int id = 0;
        if (ids->at_checked((size_t)i, &id) != 0)
            continue;
        print("TODO item", id, td_titles[i], td_states[i]);
    }
    print("TODO list", n);
}

/* Parse one "id|done|title" record with string::field. */
static int td_load_record(vector *ids, const char *line) {
    string rec = string(line);
    char f_id[32];
    char f_done[8];
    char f_title[TD_TITLE_CAP];
    int id = 0;
    int done = 0;
    int pos;
    int ok;

    if (!rec.valid()) {
        rec.drop();
        return 0;
    }
    ok = rec.field('|', 0, f_id, sizeof(f_id)) == 0 &&
         rec.field('|', 1, f_done, sizeof(f_done)) == 0 &&
         rec.field('|', 2, f_title, sizeof(f_title)) == 0;
    if (!ok || !td_parse_int(f_id, &id) || !td_parse_int(f_done, &done) ||
        done < 0 || done > 1 || id < 1) {
        rec.drop();
        return 0;
    }
    pos = td_find(ids, id);
    if (pos < 0 && ids->size() < TD_MAX_TASKS &&
        ids->push_back(id) == 0) {
        pos = (int)ids->size() - 1;
        td_states[pos] = done;
        ok = td_set_title(pos, f_title);
        if (!ok) {
            int discarded = 0;
            ids->pop_back(&discarded);
        }
    } else {
        ok = 0;
    }
    rec.drop();
    return ok;
}

/* Execute one command; returns 0 on success, nonzero on invalid input. */
static int td_execute(const char *cmd, const char *a1, const char *a2,
                      vector *ids, error *err) {
    int rc = 0;

    if (cmd == NULL) {
        td_error("usage");
        return 2;
    }
    if (strcmp(cmd, "add") == 0) {
        int id = 0;
        if (a1 == NULL || a2 == NULL || !td_parse_int(a1, &id) || id < 1) {
            td_error("bad-add");
            return 1;
        }
        if (td_find(ids, id) >= 0) {
            td_error("duplicate");
            return 1;
        }
        if (ids->size() >= TD_MAX_TASKS) {
            td_error("full");
            return 1;
        }
        if (ids->push_back(id) != 0) {
            td_error("alloc");
            return 1;
        }
        {
            int pos = (int)ids->size() - 1;
            td_states[pos] = 0;
            if (!td_set_title(pos, a2)) {
                int discarded = 0;
                ids->pop_back(&discarded);
                td_error("bad-title");
                return 1;
            }
            print("TODO add", id, td_titles[pos]);
        }
    } else if (strcmp(cmd, "list") == 0) {
        if (a1 != NULL) {
            td_error("bad-list");
            return 1;
        }
        td_list(ids);
    } else if (strcmp(cmd, "done") == 0 || strcmp(cmd, "remove") == 0) {
        int id = 0;
        int pos;
        int is_remove = strcmp(cmd, "remove") == 0;
        if (a1 == NULL || !td_parse_int(a1, &id) || id < 1) {
            td_error(is_remove ? "bad-remove" : "bad-done");
            return 1;
        }
        pos = td_find(ids, id);
        if (pos < 0) {
            td_error("not-found");
            return 1;
        }
        if (is_remove) {
            int discarded = 0;
            int j;
            int new_size;
            ids->erase((size_t)pos, &discarded);
            new_size = (int)ids->size();
            for (j = pos; j < new_size; j++) {
                if (j + 1 >= TD_MAX_TASKS)
                    break;
                __builtin_memcpy(td_titles[j], td_titles[j + 1],
                                 TD_TITLE_CAP);
                td_states[j] = td_states[j + 1];
            }
            print("TODO remove", id);
        } else {
            td_states[pos] = 1;
            print("TODO done", id);
        }
    } else if (strcmp(cmd, "save") == 0 || strcmp(cmd, "load") == 0) {
        int is_load = strcmp(cmd, "load") == 0;
        if (a1 == NULL) {
            td_error(is_load ? "bad-load" : "bad-save");
            return 1;
        }
        {
            const char *path = a1;
            file stream = file(path, is_load ? "r" : "w");
            if (!stream.is_open()) {
                error_fail(err, CPLUS_EXIT_IO, "cannot-open");
                print("TODO error", err->what());
                return 1;
            }
            if (is_load) {
                char line[TD_LINE_CAP];
                int rr;
                int loaded = 0;
                while ((rr = stream.read_line(line, sizeof(line), err)) ==
                       STDLIB_OK) {
                    if (td_load_record(ids, line))
                        loaded++;
                    if (ids->size() >= TD_MAX_TASKS)
                        break;
                }
                if (rr == STDLIB_FAIL) {
                    print("TODO error", err->what());
                    rc = 1;
                } else {
                    print("TODO load", loaded, path);
                    td_list(ids);
                }
            } else {
                int i;
                int n = (int)ids->size();
                for (i = 0; i < n; i++) {
                    int id = 0;
                    char line[TD_LINE_CAP];
                    int len;
                    if (ids->at_checked((size_t)i, &id) != 0)
                        continue;
                    len = snprintf(line, sizeof(line), "%d|%d|%s\n", id,
                                   td_states[i], td_titles[i]);
                    if (len <= 0 || len >= (int)sizeof(line) ||
                        !stream.write(line)) {
                        error_fail(err, CPLUS_EXIT_IO, "write-failed");
                        print("TODO error", err->what());
                        rc = 1;
                        break;
                    }
                }
                if (rc == 0 && stream.close(err) != STDLIB_OK) {
                    print("TODO error", err->what());
                    rc = 1;
                }
                if (rc == 0)
                    print("TODO save", n, path);
            }
            stream.drop();
        }
    } else {
        td_error("unknown-command");
        return 1;
    }
    return rc;
}

int main(int argc, char **argv) {
    vector todo_ids = vector();
    error ferr = error();
    int i = 1;
    int rc = 0;

    /* Each command is up to three tokens; "--" separates commands. */
    while (i < argc) {
        const char *cmd = td_arg(argv, (size_t)i);
        const char *a1 = NULL;
        const char *a2 = NULL;
        i++;
        if (i < argc && strcmp(td_arg(argv, (size_t)i), "--") != 0) {
            a1 = td_arg(argv, (size_t)i);
            i++;
        }
        if (i < argc && strcmp(td_arg(argv, (size_t)i), "--") != 0) {
            a2 = td_arg(argv, (size_t)i);
            i++;
        }
        /* Skip any trailing tokens of a malformed segment, then separator. */
        while (i < argc && strcmp(td_arg(argv, (size_t)i), "--") != 0) {
            td_error("bad-args");
            rc = 1;
            i++;
        }
        if (i < argc)
            i++; /* consume "--" */
        if (td_execute(cmd, a1, a2, &todo_ids, &ferr) != 0)
            rc = 1;
    }
    if (argc < 2) {
        td_error("usage");
        rc = 2;
    }

    todo_ids.drop();
    return rc;
}
