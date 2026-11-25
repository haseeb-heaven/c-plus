/* C+ side of the json_log runnable examples.
 *
 * Built with the bundled C+ driver (build/c+), NOT with plain clang, because
 * two of the checked APIs live only as static-inline C+ headers:
 *   - stdlib/json.hp : json_parse_budget / json_input_within_budget
 *   - stdlib/log.hp  : log_sink / log_stdout
 *
 * The remaining calls go to the real compiled symbols verified with nm:
 *   build/safe/libcplus_safecrt.a
 *       cplus_json_parse, cplus_json_get_int, cplus_json_get_str,
 *       cplus_json_free, cplus_safe_log{,_set_level,_get_level}
 *   build/libc/libcplus_hardened.a
 *       cplus_libc_log, cplus_libc_log_set_enabled
 *
 * Prototypes for the four cplus_json_* entry points are local: the archive
 * ships no public header for them.  They were recovered from nm + otool and
 * confirmed by a link-and-run probe (see NOTES.md).
 *
 * Output protocol: one "<api> <case> <token>" line per case, stdout only.
 * Logging back-ends write their own diagnostics to stderr; compare.sh drops
 * stderr.  All fixtures are fixed; there is no wall-clock or randomness.
 *
 * Supported build (from the repository root):
 *   ./build/c+ build benchmarks/rust_examples/json_log/harness.c \
 *     --default --sanitize=none -I. -Ithird_party/yyjson \
 *     -Lbuild/ext -lyyjson -o <out>/harness \
 *     -- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"
#include "stdlib/log.hp"
#include "stdlib/safe/safe_log.h"
#include "libc/log/log.h"

#if defined(__cplusplus)
extern "C" {
#endif
extern void *cplus_json_parse(const char *text, int *status);
extern int cplus_json_get_int(void *handle, const char *key, int64_t *out);
extern const char *cplus_json_get_str(void *handle, const char *key);
extern void cplus_json_free(void *handle);
#if defined(__cplusplus)
}
#endif

static void emit(const char *api, const char *kase, const char *token) {
    printf("%s %s %s\n", api, kase, token);
}

int main(void) {
    static const char good[] = "{\"name\":\"alpha\",\"n\":42,\"ok\":true}";
    static const char bad[] = "{\"name\":\"alpha\",\"n\":";
    char deep[66]; /* 32 '[' + '0' + 32 ']' + NUL */
    int i;
    int status = -1;
    void *handle = NULL;
    int64_t number = 0;
    const char *text = NULL;
    int rc = 0;

    for (i = 0; i < 32; ++i) {
        deep[i] = '[';
        deep[33 + i] = ']';
    }
    deep[32] = '0';
    deep[65] = '\0';

    /* ---- compiled cplus_json_parse ------------------------------------- */
    status = -1;
    handle = cplus_json_parse(good, &status);
    emit("cplus_json_parse", "valid",
         (handle != NULL && status == 0) ? "OK" : "REFUSED");
    if (handle != NULL)
        cplus_json_free(handle);

    status = -1;
    handle = cplus_json_parse(bad, &status);
    emit("cplus_json_parse", "malformed",
         (handle == NULL && status == 2) ? "REFUSED" : "OK");

    status = -1;
    handle = cplus_json_parse(NULL, &status);
    emit("cplus_json_parse", "null_args",
         (handle == NULL && status == 1) ? "REFUSED" : "OK");

    /* ---- compiled cplus_json_get_int ----------------------------------- */
    status = -1;
    handle = cplus_json_parse(good, &status);
    number = 0;
    rc = cplus_json_get_int(handle, "n", &number);
    emit("cplus_json_get_int", "valid",
         (rc == 1 && number == 42) ? "OK" : "REFUSED");
    rc = cplus_json_get_int(NULL, "n", &number);
    emit("cplus_json_get_int", "null_args", (rc == 0) ? "REFUSED" : "OK");
    if (handle != NULL)
        cplus_json_free(handle);

    /* ---- compiled cplus_json_get_str ----------------------------------- */
    status = -1;
    handle = cplus_json_parse(good, &status);
    text = cplus_json_get_str(handle, "name");
    emit("cplus_json_get_str", "valid",
         (text != NULL && strcmp(text, "alpha") == 0) ? "OK" : "REFUSED");
    text = cplus_json_get_str(NULL, "name");
    emit("cplus_json_get_str", "null_args", (text == NULL) ? "REFUSED" : "OK");
    if (handle != NULL)
        cplus_json_free(handle);

    /* ---- compiled cplus_json_free -------------------------------------- */
    status = -1;
    handle = cplus_json_parse(good, &status);
    cplus_json_free(handle);
    emit("cplus_json_free", "valid", "OK");
    cplus_json_free(NULL); /* documented no-op on a null handle */
    emit("cplus_json_free", "null_args", "OK");

    /* ---- static-inline json.hp budget API (no compiled cplus_* symbol) -- */
    {
        error e1 = error();
        json d1 = json_parse_budget(good, strlen(good), 8, &e1);
        emit("json_parse_budget", "valid", d1.valid() ? "OK" : "REFUSED");
        d1.drop();
    }
    {
        error e2 = error();
        json d2 = json_parse_budget(deep, strlen(deep), 8, &e2);
        emit("json_parse_budget", "budget", d2.valid() ? "OK" : "REFUSED");
        d2.drop();
    }
    {
        error e3 = error();
        json d3 = json_parse_budget(NULL, 0, 8, &e3);
        emit("json_parse_budget", "null_args", d3.valid() ? "OK" : "REFUSED");
        d3.drop();
    }

    /* ---- compiled cplus_safe_log*: write/read and null-argument path --- */
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_INFO);
    cplus_safe_log(CPLUS_SAFE_LOG_INFO, "cplus_safe_log", "write_read");
    emit("cplus_safe_log", "write_read",
         cplus_safe_log_get_level() == CPLUS_SAFE_LOG_INFO ? "OK" : "REFUSED");
    cplus_safe_log(CPLUS_SAFE_LOG_INFO, NULL, NULL); /* api/msg -> "(null)" */
    emit("cplus_safe_log", "null_stream", "OK");
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);

    /* ---- compiled cplus_libc_log*: errno preservation / closed fd 2 ----- */
    errno = EDOM;
    cplus_libc_log_set_enabled(1);
    cplus_libc_log("cplus_libc_log", "write_read");
    rc = (errno == EDOM); /* logger promises not to clobber errno */
    cplus_libc_log_set_enabled(0);
    emit("cplus_libc_log", "write_read", rc ? "OK" : "REFUSED");

    {
        int saved = dup(STDERR_FILENO);
        if (saved >= 0) {
            close(STDERR_FILENO); /* "null stream": write(2, ...) -> EBADF */
            cplus_libc_log_set_enabled(1);
            cplus_libc_log("cplus_libc_log", "null_stream");
            cplus_libc_log_set_enabled(0);
            (void)dup2(saved, STDERR_FILENO);
            close(saved);
        }
        emit("cplus_libc_log", "null_stream", saved >= 0 ? "OK" : "BLOCKED");
    }

    /* ---- static-inline log.hp sink API (no compiled log_sink symbol) ---- */
    {
        log_sink borrowed = log_stdout(LOG_INFO);
        int good_sink = (borrowed.valid() != 0 && borrowed.level() == LOG_INFO);
        emit("log_sink", "write_read", good_sink ? "OK" : "REFUSED");
        borrowed.drop(); /* borrowed sink must not close stdout */
    }
    {
        log_sink null_sink = log_sink(NULL, 0, LOG_INFO);
        int refused = (null_sink.valid() == 0);
        emit("log_sink", "null_stream", refused ? "REFUSED" : "OK");
        null_sink.drop();
    }

    return 0;
}
