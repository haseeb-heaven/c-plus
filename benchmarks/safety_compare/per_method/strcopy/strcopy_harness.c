/* Per-method memory-safety harness: C+ checked string-copy APIs (C99).
 *
 * One method is executed at a time (pass the method name as argv[1]); with no
 * argument every method is emitted. Output lines are exactly:
 *
 *     <method> <case> <token>
 *
 * The C+ symbols below are the prefixed safeclib entry points exported by
 * build/safe/libcplus_safecrt.a. rsize_t is size_t and errno_t is int in the
 * pinned safeclib headers, so the signatures are spelled out with those types.
 *
 * Token vocabulary (shared with strcopy.rs):
 *   OK             - call returned EOK (0) with the copy fully performed
 *   REF_NULL       - call refused a NULL dest/src (ESNULLP 400)
 *   REF_NOSPC      - call refused for insufficient dmax / truncation /
 *                    unterminated source within the bound (ESNOSPC 406)
 *   CANARY_INTACT  - refusal left the out-of-dmax guard region untouched
 *   CANARY_CORRUPT - guard region changed (would indicate a bounds bug)
 *   BLOCKED        - reserved: emitted only if a checked symbol is absent.
 *                    All six methods currently have real built symbols.
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

extern int cplus_sc_strcpy_s(char *dest, size_t dmax, const char *src);
extern int cplus_sc_strncpy_s(char *dest, size_t dmax, const char *src,
                              size_t slen);
extern int cplus_sc_strcat_s(char *dest, size_t dmax, const char *src);
extern int cplus_sc_strncat_s(char *dest, size_t dmax, const char *src,
                              size_t slen);

/* Vendored STP workers emitted by libc/src/safecrt_vendor/stp_group.c into
 * build/safe/libcplus_safecrt.a (object v_stp_group.o). BOS_UNKNOWN (-1) is
 * passed for both compile-time object-size arguments. */
#define CPLUS_BOS_UNKNOWN ((size_t)-1)
extern char *cplus_v__stpcpy_s_chk(char *dest, size_t dmax, const char *src,
                                   int *errp, size_t destbos, size_t srcbos);
extern char *cplus_v__stpncpy_s_chk(char *dest, size_t dmax, const char *src,
                                    size_t slen, int *errp, size_t destbos,
                                    size_t srcbos);

/* Keep the harness stdout to exactly the <method> <case> <token> records.
 * safeclib's default constraint handler writes diagnostics to stdout. */
typedef void (*sc_constraint_handler_t)(const char *, void *, int);
extern sc_constraint_handler_t
cplus_sc_set_str_constraint_handler_s(sc_constraint_handler_t handler);

static void silent_constraint_handler(const char *msg, void *ptr, int error) {
    (void)msg;
    (void)ptr;
    (void)error;
}

#define CPLUS_ESNULLP 400
#define CPLUS_ESNOSPC 406

#define CANARY_BYTE 0xA5u
#define UNTERM_LEN 32

static const char short_src[] = "abc";         /* strlen 3 */
static const char long_src[] = "ABCDEFGHIJ";   /* strlen 10 */
static const char unterminated[UNTERM_LEN] = {
    'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A',
    'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A'
};

static void emit(const char *method, const char *case_name, const char *tok) {
    printf("%s %s %s\n", method, case_name, tok);
}

static const char *tok_from_rc(int rc) {
    if (rc == 0) return "OK";
    if (rc == CPLUS_ESNULLP) return "REF_NULL";
    if (rc == CPLUS_ESNOSPC) return "REF_NOSPC";
    return "REF_OTHER";
}

static int canary_intact(const unsigned char *buf, size_t start, size_t len) {
    size_t i;
    for (i = 0; i < len; ++i) {
        if (buf[start + i] != CANARY_BYTE) return 0;
    }
    return 1;
}

static void m_strcpy_s(void) {
    const char *m = "strcpy_s";
    char dest[32];

    /* valid exact fit: dmax == strlen(src)+1 */
    memset(dest, 0, sizeof dest);
    emit(m, "valid_exact_fit", tok_from_rc(cplus_sc_strcpy_s(dest, 4, short_src)));

    /* null destination */
    emit(m, "null_dest", tok_from_rc(cplus_sc_strcpy_s(NULL, 4, short_src)));

    /* null source */
    memset(dest, 0, sizeof dest);
    emit(m, "null_src", tok_from_rc(cplus_sc_strcpy_s(dest, 4, NULL)));

    /* one byte too small: dmax == strlen(src) */
    memset(dest, 0, sizeof dest);
    emit(m, "one_byte_too_small",
         tok_from_rc(cplus_sc_strcpy_s(dest, 3, short_src)));

    /* unterminated source (bounded): 32 non-NUL bytes, dmax 8 */
    memset(dest, 0, sizeof dest);
    emit(m, "unterminated_bounded",
         tok_from_rc(cplus_sc_strcpy_s(dest, 8, unterminated)));

    /* truncation with dmax: 10-byte src into dmax 4 */
    memset(dest, 0, sizeof dest);
    emit(m, "truncation_dmax",
         tok_from_rc(cplus_sc_strcpy_s(dest, 4, long_src)));

    /* canary intact on refusal: guard begins at dmax */
    {
        unsigned char buf[32];
        int rc;
        memset(buf, CANARY_BYTE, sizeof buf);
        rc = cplus_sc_strcpy_s((char *)buf, 3, short_src); /* 3 < 4 needed */
        emit(m, "canary_refusal",
             (rc != 0 && canary_intact(buf, 3, sizeof buf - 3))
                 ? "CANARY_INTACT" : "CANARY_CORRUPT");
    }
}

static void m_strncpy_s(void) {
    const char *m = "strncpy_s";
    char dest[32];

    /* valid exact fit: dmax 4, slen 4 */
    memset(dest, 0, sizeof dest);
    emit(m, "valid_exact_fit",
         tok_from_rc(cplus_sc_strncpy_s(dest, 4, short_src, 4)));

    emit(m, "null_dest", tok_from_rc(cplus_sc_strncpy_s(NULL, 4, short_src, 4)));

    memset(dest, 0, sizeof dest);
    emit(m, "null_src", tok_from_rc(cplus_sc_strncpy_s(dest, 4, NULL, 4)));

    /* one byte too small: dmax 3, slen 4 */
    memset(dest, 0, sizeof dest);
    emit(m, "one_byte_too_small",
         tok_from_rc(cplus_sc_strncpy_s(dest, 3, short_src, 4)));

    /* unterminated source (bounded): slen 8 bounds the unbounded source */
    memset(dest, 0, sizeof dest);
    emit(m, "unterminated_bounded",
         tok_from_rc(cplus_sc_strncpy_s(dest, 9, unterminated, 8)));

    /* truncation with dmax: slen 3 truncates the 10-byte source */
    memset(dest, 0, sizeof dest);
    emit(m, "truncation_dmax",
         tok_from_rc(cplus_sc_strncpy_s(dest, 4, long_src, 3)));

    {
        unsigned char buf[32];
        int rc;
        memset(buf, CANARY_BYTE, sizeof buf);
        rc = cplus_sc_strncpy_s((char *)buf, 2, short_src, 4);
        emit(m, "canary_refusal",
             (rc != 0 && canary_intact(buf, 2, sizeof buf - 2))
                 ? "CANARY_INTACT" : "CANARY_CORRUPT");
    }
}

static void m_strcat_s(void) {
    const char *m = "strcat_s";
    char dest[32];

    /* valid exact fit: "xy" (2) + "abc" (3) + NUL == 6 */
    memset(dest, 0, sizeof dest);
    strcpy(dest, "xy");
    emit(m, "valid_exact_fit", tok_from_rc(cplus_sc_strcat_s(dest, 6, short_src)));

    memset(dest, 0, sizeof dest);
    emit(m, "null_dest", tok_from_rc(cplus_sc_strcat_s(NULL, 6, short_src)));

    memset(dest, 0, sizeof dest);
    strcpy(dest, "xy");
    emit(m, "null_src", tok_from_rc(cplus_sc_strcat_s(dest, 6, NULL)));

    /* one byte too small: need 6, dmax 5 */
    memset(dest, 0, sizeof dest);
    strcpy(dest, "xy");
    emit(m, "one_byte_too_small",
         tok_from_rc(cplus_sc_strcat_s(dest, 5, short_src)));

    /* unterminated source (bounded): dest "xy", 32 non-NUL bytes, dmax 8 */
    memset(dest, 0, sizeof dest);
    strcpy(dest, "xy");
    emit(m, "unterminated_bounded",
         tok_from_rc(cplus_sc_strcat_s(dest, 8, unterminated)));

    /* truncation with dmax: 10-byte src appended with dmax 6 */
    memset(dest, 0, sizeof dest);
    strcpy(dest, "xy");
    emit(m, "truncation_dmax", tok_from_rc(cplus_sc_strcat_s(dest, 6, long_src)));

    /* canary: "XY" then guard; dmax 3 forces refusal before guard */
    {
        unsigned char buf[32];
        int rc;
        memset(buf, CANARY_BYTE, sizeof buf);
        buf[0] = 'X'; buf[1] = 'Y'; buf[2] = '\0';
        rc = cplus_sc_strcat_s((char *)buf, 3, short_src);
        emit(m, "canary_refusal",
             (rc != 0 && canary_intact(buf, 3, sizeof buf - 3))
                 ? "CANARY_INTACT" : "CANARY_CORRUPT");
    }
}

static void m_strncat_s(void) {
    const char *m = "strncat_s";
    char dest[32];

    /* valid exact fit: "xy" + 3 bounded chars + NUL == 6 */
    memset(dest, 0, sizeof dest);
    strcpy(dest, "xy");
    emit(m, "valid_exact_fit",
         tok_from_rc(cplus_sc_strncat_s(dest, 6, short_src, 3)));

    memset(dest, 0, sizeof dest);
    emit(m, "null_dest", tok_from_rc(cplus_sc_strncat_s(NULL, 6, short_src, 3)));

    memset(dest, 0, sizeof dest);
    strcpy(dest, "xy");
    emit(m, "null_src", tok_from_rc(cplus_sc_strncat_s(dest, 6, NULL, 3)));

    /* one byte too small: need 6, dmax 5 */
    memset(dest, 0, sizeof dest);
    strcpy(dest, "xy");
    emit(m, "one_byte_too_small",
         tok_from_rc(cplus_sc_strncat_s(dest, 5, short_src, 3)));

    /* unterminated source (bounded): slen 5 bounds the unbounded source */
    memset(dest, 0, sizeof dest);
    strcpy(dest, "xy");
    emit(m, "unterminated_bounded",
         tok_from_rc(cplus_sc_strncat_s(dest, 8, unterminated, 5)));

    /* truncation with dmax: slen 2 truncates the 10-byte source */
    memset(dest, 0, sizeof dest);
    strcpy(dest, "xy");
    emit(m, "truncation_dmax",
         tok_from_rc(cplus_sc_strncat_s(dest, 5, long_src, 2)));

    /* canary: "XY" then guard; dmax 3 forces refusal before guard */
    {
        unsigned char buf[32];
        int rc;
        memset(buf, CANARY_BYTE, sizeof buf);
        buf[0] = 'X'; buf[1] = 'Y'; buf[2] = '\0';
        rc = cplus_sc_strncat_s((char *)buf, 3, short_src, 5);
        emit(m, "canary_refusal",
             (rc != 0 && canary_intact(buf, 3, sizeof buf - 3))
                 ? "CANARY_INTACT" : "CANARY_CORRUPT");
    }
}

/* stpcpy_s returns a pointer to the terminating NUL instead of the dest base;
 * the checked worker reports the contract result through *errp. Token mapping
 * is identical to strcpy_s because the safety contract is identical. */
static void m_stpcpy_s(void) {
    const char *m = "stpcpy_s";
    char dest[32];
    int err;

    memset(dest, 0, sizeof dest);
    (void)cplus_v__stpcpy_s_chk(dest, 4, short_src, &err,
                                CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
    emit(m, "valid_exact_fit", tok_from_rc(err));

    err = -1;
    (void)cplus_v__stpcpy_s_chk(NULL, 4, short_src, &err,
                                CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
    emit(m, "null_dest", tok_from_rc(err));

    memset(dest, 0, sizeof dest);
    err = -1;
    (void)cplus_v__stpcpy_s_chk(dest, 4, NULL, &err,
                                CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
    emit(m, "null_src", tok_from_rc(err));

    memset(dest, 0, sizeof dest);
    err = -1;
    (void)cplus_v__stpcpy_s_chk(dest, 3, short_src, &err,
                                CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
    emit(m, "one_byte_too_small", tok_from_rc(err));

    memset(dest, 0, sizeof dest);
    err = -1;
    (void)cplus_v__stpcpy_s_chk(dest, 8, unterminated, &err,
                                CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
    emit(m, "unterminated_bounded", tok_from_rc(err));

    memset(dest, 0, sizeof dest);
    err = -1;
    (void)cplus_v__stpcpy_s_chk(dest, 4, long_src, &err,
                                CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
    emit(m, "truncation_dmax", tok_from_rc(err));

    {
        unsigned char buf[32];
        memset(buf, CANARY_BYTE, sizeof buf);
        err = -1;
        (void)cplus_v__stpcpy_s_chk((char *)buf, 3, short_src, &err,
                                    CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
        emit(m, "canary_refusal",
             (err != 0 && canary_intact(buf, 3, sizeof buf - 3))
                 ? "CANARY_INTACT" : "CANARY_CORRUPT");
    }
}

static void m_stpncpy_s(void) {
    const char *m = "stpncpy_s";
    char dest[32];
    int err;

    memset(dest, 0, sizeof dest);
    (void)cplus_v__stpncpy_s_chk(dest, 4, short_src, 4, &err,
                                 CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
    emit(m, "valid_exact_fit", tok_from_rc(err));

    err = -1;
    (void)cplus_v__stpncpy_s_chk(NULL, 4, short_src, 4, &err,
                                 CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
    emit(m, "null_dest", tok_from_rc(err));

    memset(dest, 0, sizeof dest);
    err = -1;
    (void)cplus_v__stpncpy_s_chk(dest, 4, NULL, 4, &err,
                                 CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
    emit(m, "null_src", tok_from_rc(err));

    memset(dest, 0, sizeof dest);
    err = -1;
    (void)cplus_v__stpncpy_s_chk(dest, 3, short_src, 4, &err,
                                 CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
    emit(m, "one_byte_too_small", tok_from_rc(err));

    memset(dest, 0, sizeof dest);
    err = -1;
    (void)cplus_v__stpncpy_s_chk(dest, 9, unterminated, 8, &err,
                                 CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
    emit(m, "unterminated_bounded", tok_from_rc(err));

    memset(dest, 0, sizeof dest);
    err = -1;
    (void)cplus_v__stpncpy_s_chk(dest, 4, long_src, 3, &err,
                                 CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
    emit(m, "truncation_dmax", tok_from_rc(err));

    {
        unsigned char buf[32];
        memset(buf, CANARY_BYTE, sizeof buf);
        err = -1;
        (void)cplus_v__stpncpy_s_chk((char *)buf, 2, short_src, 4, &err,
                                     CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
        emit(m, "canary_refusal",
             (err != 0 && canary_intact(buf, 2, sizeof buf - 2))
                 ? "CANARY_INTACT" : "CANARY_CORRUPT");
    }
}

static int want(const char *arg, const char *method) {
    return arg == NULL || strcmp(arg, method) == 0;
}

int main(int argc, char **argv) {
    const char *only = (argc > 1) ? argv[1] : NULL;

    (void)cplus_sc_set_str_constraint_handler_s(&silent_constraint_handler);

    if (want(only, "strcpy_s"))  m_strcpy_s();
    if (want(only, "strncpy_s")) m_strncpy_s();
    if (want(only, "strcat_s"))  m_strcat_s();
    if (want(only, "strncat_s")) m_strncat_s();
    if (want(only, "stpcpy_s"))  m_stpcpy_s();
    if (want(only, "stpncpy_s")) m_stpncpy_s();
    return 0;
}
