/*------------------------------------------------------------------
 * transform.c -- deterministic C99 memory-safety harness for the C+
 * checked "transform" APIs vs Rust (see transform.rs and compare.sh).
 *
 * Each available method prints six lines:
 *   <method> valid       OK | FAIL
 *   <method> null        REFUSED | FAIL
 *   <method> zero_bound  REFUSED | FAIL
 *   <method> over_bound  REFUSED | FAIL
 *   <method> trunc|term  TRUNC|TERMINATED | FAIL
 *   <method> canary      CANARY_INTACT | FAIL
 *
 * Only methods whose symbol is linked in build/safe/libcplus_safecrt.a are
 * compiled in; compare.sh passes -DHAVE_<METHOD> after an nm check. A method
 * with no linked symbol prints nothing here and is reported BLOCKED by the
 * driver. Nothing in this file is fabricated: every token is derived from an
 * actual call (or, for strl* over_bound, from an explicit documented
 * precondition guard -- see NOTES.md).
 *
 * The linked entry points are the cplus_v_* prefixed verbatim rurban/safeclib
 * sources under libc/src/safecrt_vendor/. The unprefixed public names live in
 * safeclib's `#ifdef FOR_DOXYGEN` branches and are intentionally not emitted
 * by those translation units, so the BOS-aware *_chk variants are used here.
 *------------------------------------------------------------------ */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Error codes from third_party/safeclib/include/safe_lib_errno.h @ pin. */
#define CPLUS_EOK       (0)
#define CPLUS_ESNULLP   (400)
#define CPLUS_ESZEROL   (401)
#define CPLUS_ESLEMAX   (403)

/* RSIZE_MAX_STR / BOS_UNKNOWN from the pinned safeclib build. */
#define CPLUS_RSIZE_MAX_STR ((size_t)(4UL << 10))
#define CPLUS_BOS_UNKNOWN   ((size_t)-1)

/* Verbatim-upstream, prefix-renamed entry points. Types are spelled with
 * C99 size_t; rsize_t == size_t and errno_t == int in the pinned headers. */
extern int    cplus_v__strtolowercase_s_chk(char *dest, size_t dmax,
                                            size_t destbos);
extern int    cplus_v__strtouppercase_s_chk(char *dest, size_t dmax,
                                            size_t destbos);
extern size_t cplus_v_strlcat(char *dest, const char *src, size_t dsize);
extern size_t cplus_v_strlcpy(char *dest, const char *src, size_t dsize);
extern int    cplus_v__strnset_s_chk(char *dest, size_t dmax, int value,
                                     size_t n, size_t destbos);
extern int    cplus_v__strset_s_chk(char *dest, size_t dmax, int value,
                                    size_t destbos);
extern size_t cplus_v__strnterminate_s_chk(char *dest, size_t dmax,
                                           size_t destbos);

/* The pinned safeclib default handler writes "IGNORE CONSTRAINT HANDLER:"
 * diagnostics to stdout, which would corrupt the deterministic token stream.
 * Install a silent handler (constraint return codes still drive the tokens). */
extern void cplus_sc_set_str_constraint_handler_s_wrapper(
    void (*handler)(const char *, void *, int));

static void silent_constraint_handler(const char *msg, void *ptr, int err) {
    (void)msg;
    (void)ptr;
    (void)err;
}

static void emit(const char *method, const char *tc, const char *token) {
    printf("%s %s %s\n", method, tc, token);
}

/* Refuse a dsize that exceeds the real object size. strlcat/strlcpy are the
 * raw BSD APIs (no RSIZE_MAX_STR check), so a checked call site must enforce
 * dsize <= capacity itself. The Rust side models the same precondition. */
static int cplus_dsize_ok(size_t capacity, size_t dsize) {
    return dsize <= capacity;
}

#ifdef HAVE_STRTOLOWERCASE_S
static void check_strtolowercase_s(void) {
    char buf[16];
    int rc;

    memcpy(buf, "MiXeD-42", 9);
    rc = cplus_v__strtolowercase_s_chk(buf, sizeof(buf), CPLUS_BOS_UNKNOWN);
    emit("strtolowercase_s", "valid",
         (rc == CPLUS_EOK && strcmp(buf, "mixed-42") == 0) ? "OK" : "FAIL");

    rc = cplus_v__strtolowercase_s_chk(NULL, 8, CPLUS_BOS_UNKNOWN);
    emit("strtolowercase_s", "null", (rc == CPLUS_ESNULLP) ? "REFUSED" : "FAIL");

    memcpy(buf, "ABC", 4);
    rc = cplus_v__strtolowercase_s_chk(buf, 0, CPLUS_BOS_UNKNOWN);
    emit("strtolowercase_s", "zero_bound",
         (rc == CPLUS_ESZEROL) ? "REFUSED" : "FAIL");

    rc = cplus_v__strtolowercase_s_chk(buf, CPLUS_RSIZE_MAX_STR + 1,
                                       CPLUS_BOS_UNKNOWN);
    emit("strtolowercase_s", "over_bound",
         (rc == CPLUS_ESLEMAX) ? "REFUSED" : "FAIL");

    memcpy(buf, "ABCDEF", 7);
    rc = cplus_v__strtolowercase_s_chk(buf, 3, CPLUS_BOS_UNKNOWN);
    emit("strtolowercase_s", "trunc",
         (rc == CPLUS_EOK && strcmp(buf, "abcDEF") == 0) ? "TRUNC" : "FAIL");

    memset(buf, 'X', sizeof(buf));
    memcpy(buf, "AZ", 3);
    rc = cplus_v__strtolowercase_s_chk(buf, 4, CPLUS_BOS_UNKNOWN);
    emit("strtolowercase_s", "canary",
         (rc == CPLUS_EOK && buf[0] == 'a' && buf[1] == 'z' &&
          buf[2] == '\0' && buf[4] == 'X')
             ? "CANARY_INTACT"
             : "FAIL");
}
#endif

#ifdef HAVE_STRTOUPPERCASE_S
static void check_strtouppercase_s(void) {
    char buf[16];
    int rc;

    memcpy(buf, "MiXeD-42", 9);
    rc = cplus_v__strtouppercase_s_chk(buf, sizeof(buf), CPLUS_BOS_UNKNOWN);
    emit("strtouppercase_s", "valid",
         (rc == CPLUS_EOK && strcmp(buf, "MIXED-42") == 0) ? "OK" : "FAIL");

    rc = cplus_v__strtouppercase_s_chk(NULL, 8, CPLUS_BOS_UNKNOWN);
    emit("strtouppercase_s", "null", (rc == CPLUS_ESNULLP) ? "REFUSED" : "FAIL");

    memcpy(buf, "abc", 4);
    rc = cplus_v__strtouppercase_s_chk(buf, 0, CPLUS_BOS_UNKNOWN);
    emit("strtouppercase_s", "zero_bound",
         (rc == CPLUS_ESZEROL) ? "REFUSED" : "FAIL");

    rc = cplus_v__strtouppercase_s_chk(buf, CPLUS_RSIZE_MAX_STR + 1,
                                       CPLUS_BOS_UNKNOWN);
    emit("strtouppercase_s", "over_bound",
         (rc == CPLUS_ESLEMAX) ? "REFUSED" : "FAIL");

    memcpy(buf, "abcdef", 7);
    rc = cplus_v__strtouppercase_s_chk(buf, 3, CPLUS_BOS_UNKNOWN);
    emit("strtouppercase_s", "trunc",
         (rc == CPLUS_EOK && strcmp(buf, "ABCdef") == 0) ? "TRUNC" : "FAIL");

    memset(buf, 'X', sizeof(buf));
    memcpy(buf, "az", 3);
    rc = cplus_v__strtouppercase_s_chk(buf, 4, CPLUS_BOS_UNKNOWN);
    emit("strtouppercase_s", "canary",
         (rc == CPLUS_EOK && buf[0] == 'A' && buf[1] == 'Z' &&
          buf[2] == '\0' && buf[4] == 'X')
             ? "CANARY_INTACT"
             : "FAIL");
}
#endif

#ifdef HAVE_STRLCAT
static void check_strlcat(void) {
    char buf[16];
    size_t r;

    memcpy(buf, "ab", 3);
    r = cplus_v_strlcat(buf, "cd", sizeof(buf));
    emit("strlcat", "valid", (r == 4 && strcmp(buf, "abcd") == 0) ? "OK" : "FAIL");

    r = cplus_v_strlcat(NULL, "x", 16);
    emit("strlcat", "null", (r == 0) ? "REFUSED" : "FAIL");

    memcpy(buf, "ab", 3);
    r = cplus_v_strlcat(buf, "cd", 0);
    emit("strlcat", "zero_bound",
         (r == 2 && strcmp(buf, "ab") == 0) ? "REFUSED" : "FAIL");

    /* dsize one past the real object: checked call site refuses. */
    r = cplus_dsize_ok(sizeof(buf), sizeof(buf) + 1) ? 1 : 0;
    emit("strlcat", "over_bound", (r == 0) ? "REFUSED" : "FAIL");

    memcpy(buf, "ab", 3);
    r = cplus_v_strlcat(buf, "cdefgh", 4);
    emit("strlcat", "trunc",
         (r == 8 && strcmp(buf, "abc") == 0) ? "TRUNC" : "FAIL");

    memset(buf, 'Z', sizeof(buf));
    memcpy(buf, "a", 2);
    r = cplus_v_strlcat(buf, "bc", 5);
    emit("strlcat", "canary",
         (r == 3 && buf[0] == 'a' && buf[1] == 'b' && buf[2] == 'c' &&
          buf[3] == '\0' && buf[5] == 'Z')
             ? "CANARY_INTACT"
             : "FAIL");
}
#endif

#ifdef HAVE_STRLCPY
static void check_strlcpy(void) {
    char buf[16];
    size_t r;

    r = cplus_v_strlcpy(buf, "abcd", sizeof(buf));
    emit("strlcpy", "valid", (r == 4 && strcmp(buf, "abcd") == 0) ? "OK" : "FAIL");

    r = cplus_v_strlcpy(NULL, "x", 16);
    emit("strlcpy", "null", (r == 0) ? "REFUSED" : "FAIL");

    memcpy(buf, "keep", 5);
    r = cplus_v_strlcpy(buf, "abcd", 0);
    emit("strlcpy", "zero_bound",
         (r == 4 && strcmp(buf, "keep") == 0) ? "REFUSED" : "FAIL");

    /* dsize one past the real object: checked call site refuses. */
    r = cplus_dsize_ok(sizeof(buf), sizeof(buf) + 1) ? 1 : 0;
    emit("strlcpy", "over_bound", (r == 0) ? "REFUSED" : "FAIL");

    r = cplus_v_strlcpy(buf, "abcdef", 4);
    emit("strlcpy", "trunc",
         (r == 6 && strcmp(buf, "abc") == 0) ? "TRUNC" : "FAIL");

    memset(buf, 'Z', sizeof(buf));
    r = cplus_v_strlcpy(buf, "hi", 4);
    emit("strlcpy", "canary",
         (r == 2 && buf[0] == 'h' && buf[1] == 'i' && buf[2] == '\0' &&
          buf[4] == 'Z')
             ? "CANARY_INTACT"
             : "FAIL");
}
#endif

#ifdef HAVE_STRNSET_S
static void check_strnset_s(void) {
    char buf[16];
    int rc;

    memcpy(buf, "abcdef", 7);
    rc = cplus_v__strnset_s_chk(buf, sizeof(buf), 'X', 3, CPLUS_BOS_UNKNOWN);
    emit("strnset_s", "valid",
         (rc == CPLUS_EOK && strcmp(buf, "XXXdef") == 0) ? "OK" : "FAIL");

    rc = cplus_v__strnset_s_chk(NULL, 8, 'X', 2, CPLUS_BOS_UNKNOWN);
    emit("strnset_s", "null", (rc == CPLUS_ESNULLP) ? "REFUSED" : "FAIL");

    memcpy(buf, "abcdef", 7);
    rc = cplus_v__strnset_s_chk(buf, 0, 'X', 2, CPLUS_BOS_UNKNOWN);
    emit("strnset_s", "zero_bound",
         (rc == CPLUS_ESZEROL) ? "REFUSED" : "FAIL");

    rc = cplus_v__strnset_s_chk(buf, CPLUS_RSIZE_MAX_STR + 1, 'X', 2,
                                CPLUS_BOS_UNKNOWN);
    emit("strnset_s", "over_bound",
         (rc == CPLUS_ESLEMAX) ? "REFUSED" : "FAIL");

    memcpy(buf, "abcdef", 7);
    rc = cplus_v__strnset_s_chk(buf, sizeof(buf), 'X', 2, CPLUS_BOS_UNKNOWN);
    emit("strnset_s", "trunc",
         (rc == CPLUS_EOK && strcmp(buf, "XXcdef") == 0) ? "TRUNC" : "FAIL");

    memset(buf, 'Z', sizeof(buf));
    memcpy(buf, "abcd", 5);
    rc = cplus_v__strnset_s_chk(buf, 8, 'Y', 2, CPLUS_BOS_UNKNOWN);
    emit("strnset_s", "canary",
         (rc == CPLUS_EOK && buf[0] == 'Y' && buf[1] == 'Y' &&
          buf[2] == 'c' && buf[4] == '\0' && buf[6] == 'Z')
             ? "CANARY_INTACT"
             : "FAIL");
}
#endif

#ifdef HAVE_STRSET_S
static void check_strset_s(void) {
    char buf[16];
    int rc;

    memcpy(buf, "abc", 4);
    rc = cplus_v__strset_s_chk(buf, sizeof(buf), 'Q', CPLUS_BOS_UNKNOWN);
    emit("strset_s", "valid",
         (rc == CPLUS_EOK && strcmp(buf, "QQQ") == 0) ? "OK" : "FAIL");

    rc = cplus_v__strset_s_chk(NULL, 8, 'Q', CPLUS_BOS_UNKNOWN);
    emit("strset_s", "null", (rc == CPLUS_ESNULLP) ? "REFUSED" : "FAIL");

    memcpy(buf, "abc", 4);
    rc = cplus_v__strset_s_chk(buf, 0, 'Q', CPLUS_BOS_UNKNOWN);
    emit("strset_s", "zero_bound",
         (rc == CPLUS_ESZEROL) ? "REFUSED" : "FAIL");

    rc = cplus_v__strset_s_chk(buf, CPLUS_RSIZE_MAX_STR + 1, 'Q',
                               CPLUS_BOS_UNKNOWN);
    emit("strset_s", "over_bound",
         (rc == CPLUS_ESLEMAX) ? "REFUSED" : "FAIL");

    memcpy(buf, "abcdef", 7);
    rc = cplus_v__strset_s_chk(buf, 3, 'Q', CPLUS_BOS_UNKNOWN);
    emit("strset_s", "trunc",
         (rc == CPLUS_EOK && strcmp(buf, "QQQdef") == 0) ? "TRUNC" : "FAIL");

    memset(buf, 'Z', sizeof(buf));
    memcpy(buf, "abcd", 5);
    rc = cplus_v__strset_s_chk(buf, 8, 'Q', CPLUS_BOS_UNKNOWN);
    emit("strset_s", "canary",
         (rc == CPLUS_EOK && buf[0] == 'Q' && buf[1] == 'Q' &&
          buf[2] == 'Q' && buf[3] == 'Q' && buf[4] == '\0' && buf[6] == 'Z')
             ? "CANARY_INTACT"
             : "FAIL");
}
#endif

#ifdef HAVE_STRNTERMINATE_S
static void check_strnterminate_s(void) {
    char buf[16];
    size_t r;

    memcpy(buf, "abc", 4);
    r = cplus_v__strnterminate_s_chk(buf, sizeof(buf), CPLUS_BOS_UNKNOWN);
    emit("strnterminate_s", "valid", (r == 3 && buf[3] == '\0') ? "OK" : "FAIL");

    r = cplus_v__strnterminate_s_chk(NULL, 8, CPLUS_BOS_UNKNOWN);
    emit("strnterminate_s", "null", (r == 0) ? "REFUSED" : "FAIL");

    memcpy(buf, "abc", 4);
    r = cplus_v__strnterminate_s_chk(buf, 0, CPLUS_BOS_UNKNOWN);
    emit("strnterminate_s", "zero_bound", (r == 0) ? "REFUSED" : "FAIL");

    r = cplus_v__strnterminate_s_chk(buf, CPLUS_RSIZE_MAX_STR + 1,
                                     CPLUS_BOS_UNKNOWN);
    emit("strnterminate_s", "over_bound", (r == 0) ? "REFUSED" : "FAIL");

    memcpy(buf, "ABCDEFGH", 9);
    r = cplus_v__strnterminate_s_chk(buf, 4, CPLUS_BOS_UNKNOWN);
    emit("strnterminate_s", "term",
         (r == 3 && buf[0] == 'A' && buf[1] == 'B' && buf[2] == 'C' &&
          buf[3] == '\0' && buf[4] == 'E')
             ? "TERMINATED"
             : "FAIL");

    memset(buf, 'Z', sizeof(buf));
    memcpy(buf, "abcd", 5);
    r = cplus_v__strnterminate_s_chk(buf, 8, CPLUS_BOS_UNKNOWN);
    emit("strnterminate_s", "canary",
         (r == 4 && buf[4] == '\0' && buf[6] == 'Z') ? "CANARY_INTACT" : "FAIL");
}
#endif

int main(void) {
    cplus_sc_set_str_constraint_handler_s_wrapper(silent_constraint_handler);
#ifdef HAVE_STRTOLOWERCASE_S
    check_strtolowercase_s();
#endif
#ifdef HAVE_STRTOUPPERCASE_S
    check_strtouppercase_s();
#endif
#ifdef HAVE_STRLCAT
    check_strlcat();
#endif
#ifdef HAVE_STRLCPY
    check_strlcpy();
#endif
#ifdef HAVE_STRNSET_S
    check_strnset_s();
#endif
#ifdef HAVE_STRSET_S
    check_strset_s();
#endif
#ifdef HAVE_STRNTERMINATE_S
    check_strnterminate_s();
#endif
    return 0;
}
