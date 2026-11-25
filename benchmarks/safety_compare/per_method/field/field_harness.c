/* field_harness.c -- deterministic per-method memory-safety probe for the
 * C+ checked "field / first-last / classify" API family.
 *
 * For every AVAILABLE method this prints exactly six lines:
 *
 *     <method> <case> <token>
 *
 * cases  : valid | null | zero | oneover | delimiter | canary
 * tokens : result token for `valid', REFUSED for null/zero/oneover,
 *          DELIM_OK for delimiter, CANARY_INTACT (or CANARY_CORRUPT) for
 *          canary.
 *
 * A method is compiled in only when the driver passes -DHAVE_<METHOD>;
 * the driver derives that set from `nm build/safe/libcplus_safecrt.a' so
 * nothing is ever claimed that is not actually present in the archive.
 *
 * Build (see compare.sh / NOTES.md):
 *   /usr/bin/clang -std=c99 -Ilibc/install-include \
 *       -Ithird_party/safeclib/include \
 *       <HAVE defines> field_harness.c \
 *       build/safe/libcplus_safecrt.a -o field_harness
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/* Prototypes of the cplus_v_* checked entry points (upstream _chk forms).
 * rsize_t == size_t and errno_t == int at the pinned safeclib version.  */
extern int  cplus_v__strcmpfld_s_chk(const char *dest, size_t dmax,
                                     const char *src, int *resultp,
                                     size_t destbos);
extern int  cplus_v__strcpyfld_s_chk(char *dest, size_t dmax,
                                     const char *src, size_t slen,
                                     size_t destbos);
extern int  cplus_v__strcoll_s_chk(const char *dest, size_t dmax,
                                   const char *src, int *resultp,
                                   size_t destbos);
extern int  cplus_v__strfirstchar_s_chk(char *dest, size_t dmax, char c,
                                        char **firstp, size_t destbos);
extern int  cplus_v__strlastchar_s_chk(char *dest, size_t dmax, char c,
                                       char **lastp, size_t destbos);
extern int  cplus_v__strfirstdiff_s_chk(const char *dest, size_t dmax,
                                        const char *src, size_t *resultp,
                                        size_t destbos);
extern int  cplus_v__strlastdiff_s_chk(const char *dest, size_t dmax,
                                       const char *src, size_t *resultp,
                                       size_t destbos);
/* Classification family (currently BLOCKED; guarded by HAVE_* below). */
extern bool cplus_v__strisalphanumeric_s_chk(const char *dest, size_t dmax,
                                             size_t destbos);
extern bool cplus_v__strisascii_s_chk(const char *dest, size_t dmax,
                                      size_t destbos);
extern bool cplus_v__strisdigit_s_chk(const char *dest, size_t dmax,
                                      size_t destbos);
extern bool cplus_v__strishex_s_chk(const char *dest, size_t dmax,
                                    size_t destbos);

/* Constraint handler: silence stderr and record that a runtime-constraint
 * violation fired. This lets bool-returning stris* methods report REFUSED. */
static int g_violation = 0;
static void field_silent_handler(const char *msg, void *ptr, int error)
{
    (void)msg; (void)ptr; (void)error;
    g_violation = 1;
}
extern void cplus_sc_set_str_constraint_handler_s(void (*)(const char *,
                                                            void *, int));

#define EMIT(m, c, t) printf("%s %s %s\n", (m), (c), (t))

/* Large source buffers so "oneover" cases cannot be confounded by an
 * undersized src argument. g_src begins "abc" (copy source); g_cmp_src
 * begins "abd" (comparison source, so "abc" vs "abd" yields CMP_LT). */
static char g_src[128];
static char g_cmp_src[128];
static void fill_src(char *buf, const char *s)
{
    memset(buf, 'z', 128);
    memcpy(buf, s, strlen(s));
}

int main(void)
{
    setlocale(LC_ALL, "C");
    cplus_sc_set_str_constraint_handler_s(field_silent_handler);
    fill_src(g_src, "abcdefgh");
    fill_src(g_cmp_src, "abd");

    /* ---------------------------- strcmpfld_s ---------------------------- */
#ifdef HAVE_STRCMPFLD_S
    {
        char dest[16];
        int rc, res;
        memset(dest, 0, sizeof(dest));
        memcpy(dest, "abc", 3);
        rc = cplus_v__strcmpfld_s_chk(dest, 3, g_cmp_src, &res, sizeof(dest));
        EMIT("strcmpfld_s", "valid", (rc == 0 && res < 0) ? "CMP_LT" : "BAD");

        g_violation = 0; res = 123;
        rc = cplus_v__strcmpfld_s_chk(NULL, 3, g_src, &res, sizeof(dest));
        EMIT("strcmpfld_s", "null",
             (rc != 0 || g_violation) ? "REFUSED" : "BAD");

        g_violation = 0; res = 123;
        rc = cplus_v__strcmpfld_s_chk(dest, 0, g_src, &res, sizeof(dest));
        EMIT("strcmpfld_s", "zero",
             (rc != 0 || g_violation) ? "REFUSED" : "BAD");

        g_violation = 0; res = 123;
        rc = cplus_v__strcmpfld_s_chk(dest, sizeof(dest) + 1, g_src, &res,
                                      sizeof(dest));
        EMIT("strcmpfld_s", "oneover",
             (rc != 0 || g_violation) ? "REFUSED" : "BAD");

        g_violation = 0; res = 123;
        memset(dest, 0, sizeof(dest));
        dest[0] = 'a'; dest[1] = '\0'; dest[2] = 'b';
        rc = cplus_v__strcmpfld_s_chk(dest, 3, "a\0c", &res, sizeof(dest));
        EMIT("strcmpfld_s", "delimiter",
             (rc == 0 && res < 0) ? "DELIM_OK" : "BAD");

        {
            char buf[16];
            int i, ok = 1;
            memset(buf, 0, sizeof(buf));
            memcpy(buf, "xy", 2);
            for (i = 8; i < 12; ++i) buf[i] = (char)0x5A;
            g_violation = 0;
            (void)cplus_v__strcmpfld_s_chk(buf, 4, g_src, &res, 8);
            for (i = 8; i < 12; ++i) if (buf[i] != (char)0x5A) ok = 0;
            EMIT("strcmpfld_s", "canary", ok ? "CANARY_INTACT" : "CANARY_CORRUPT");
        }
    }
#endif

    /* ---------------------------- strcpyfld_s ---------------------------- */
#ifdef HAVE_STRCPYFLD_S
    {
        char dest[16];
        int rc;
        memset(dest, 0x7E, sizeof(dest));
        rc = cplus_v__strcpyfld_s_chk(dest, 6, g_src, 3, sizeof(dest));
        EMIT("strcpyfld_s", "valid",
             (rc == 0 && memcmp(dest, "abc", 3) == 0) ? "COPY_OK" : "BAD");

        g_violation = 0;
        rc = cplus_v__strcpyfld_s_chk(NULL, 6, g_src, 3, sizeof(dest));
        EMIT("strcpyfld_s", "null",
             (rc != 0 || g_violation) ? "REFUSED" : "BAD");

        g_violation = 0;
        rc = cplus_v__strcpyfld_s_chk(dest, 0, g_src, 3, sizeof(dest));
        EMIT("strcpyfld_s", "zero",
             (rc != 0 || g_violation) ? "REFUSED" : "BAD");

        g_violation = 0;
        memset(dest, 0x7E, sizeof(dest));
        rc = cplus_v__strcpyfld_s_chk(dest, 4, g_src, 5, sizeof(dest));
        EMIT("strcpyfld_s", "oneover",
             (rc != 0 || g_violation) ? "REFUSED" : "BAD");

        g_violation = 0;
        memset(dest, 0x7E, sizeof(dest));
        rc = cplus_v__strcpyfld_s_chk(dest, 4, "a\0bZ", 3, sizeof(dest));
        EMIT("strcpyfld_s", "delimiter",
             (rc == 0 && dest[0] == 'a' && dest[1] == '\0' &&
              dest[2] == 'b') ? "DELIM_OK" : "BAD");

        {
            char buf[16];
            int i, ok = 1;
            memset(buf, (int)0x5A, sizeof(buf));
            g_violation = 0;
            (void)cplus_v__strcpyfld_s_chk(buf, 6, g_src, 3, 8);
            for (i = 6; i < 16; ++i) if (buf[i] != (char)0x5A) ok = 0;
            EMIT("strcpyfld_s", "canary", ok ? "CANARY_INTACT" : "CANARY_CORRUPT");
        }
    }
#endif

    /* ------------------------------ strcoll_s ---------------------------- */
#ifdef HAVE_STRCOLL_S
    {
        char dest[16];
        int rc, res;
        strcpy(dest, "abc");
        rc = cplus_v__strcoll_s_chk(dest, 4, g_cmp_src, &res, sizeof(dest));
        EMIT("strcoll_s", "valid", (rc == 0 && res < 0) ? "CMP_LT" : "BAD");

        g_violation = 0; res = 123;
        rc = cplus_v__strcoll_s_chk(NULL, 4, g_src, &res, sizeof(dest));
        EMIT("strcoll_s", "null",
             (rc != 0 || g_violation) ? "REFUSED" : "BAD");

        g_violation = 0; res = 123;
        rc = cplus_v__strcoll_s_chk(dest, 0, g_src, &res, sizeof(dest));
        EMIT("strcoll_s", "zero",
             (rc != 0 || g_violation) ? "REFUSED" : "BAD");

        g_violation = 0; res = 123;
        rc = cplus_v__strcoll_s_chk(dest, sizeof(dest) + 1, g_src, &res,
                                    sizeof(dest));
        EMIT("strcoll_s", "oneover",
             (rc != 0 || g_violation) ? "REFUSED" : "BAD");

        g_violation = 0; res = 123;
        strcpy(dest, "ab");
        rc = cplus_v__strcoll_s_chk(dest, 4, "ab\0XYZ", &res, sizeof(dest));
        EMIT("strcoll_s", "delimiter",
             (rc == 0 && res == 0) ? "DELIM_OK" : "BAD");

        {
            char buf[16];
            int i, ok = 1;
            memset(buf, 0, sizeof(buf));
            strcpy(buf, "ab");
            for (i = 8; i < 12; ++i) buf[i] = (char)0x5A;
            g_violation = 0;
            (void)cplus_v__strcoll_s_chk(buf, 4, g_src, &res, 8);
            for (i = 8; i < 12; ++i) if (buf[i] != (char)0x5A) ok = 0;
            EMIT("strcoll_s", "canary", ok ? "CANARY_INTACT" : "CANARY_CORRUPT");
        }
    }
#endif

    /* --------------------------- strfirstchar_s -------------------------- */
#ifdef HAVE_STRFIRSTCHAR_S
    {
        char dest[16];
        char *fp;
        int rc;
        strcpy(dest, "abcabc");
        rc = cplus_v__strfirstchar_s_chk(dest, 7, 'c', &fp, sizeof(dest));
        EMIT("strfirstchar_s", "valid",
             (rc == 0 && fp == dest + 2) ? "POS_2" : "BAD");

        g_violation = 0; fp = (char *)1;
        rc = cplus_v__strfirstchar_s_chk(NULL, 7, 'c', &fp, sizeof(dest));
        EMIT("strfirstchar_s", "null",
             (rc != 0 || g_violation) ? "REFUSED" : "BAD");

        g_violation = 0; fp = (char *)1;
        rc = cplus_v__strfirstchar_s_chk(dest, 0, 'c', &fp, sizeof(dest));
        EMIT("strfirstchar_s", "zero",
             (rc != 0 || g_violation) ? "REFUSED" : "BAD");

        g_violation = 0; fp = (char *)1;
        rc = cplus_v__strfirstchar_s_chk(dest, sizeof(dest) + 1, 'c', &fp,
                                         sizeof(dest));
        EMIT("strfirstchar_s", "oneover",
             (rc != 0 || g_violation) ? "REFUSED" : "BAD");

        g_violation = 0; fp = (char *)1;
        strcpy(dest, "ab");
        dest[2] = '\0'; dest[3] = 'c';
        rc = cplus_v__strfirstchar_s_chk(dest, 8, 'c', &fp, sizeof(dest));
        EMIT("strfirstchar_s", "delimiter",
             (rc != 0 && fp == NULL) ? "DELIM_OK" : "BAD");

        {
            char buf[16];
            int i, ok = 1;
            memset(buf, 0, sizeof(buf));
            strcpy(buf, "ab");
            for (i = 8; i < 12; ++i) buf[i] = (char)0x5A;
            g_violation = 0;
            (void)cplus_v__strfirstchar_s_chk(buf, 4, 'b', &fp, 8);
            for (i = 8; i < 12; ++i) if (buf[i] != (char)0x5A) ok = 0;
            EMIT("strfirstchar_s", "canary", ok ? "CANARY_INTACT" : "CANARY_CORRUPT");
        }
    }
#endif

    /* --------------------------- strlastchar_s --------------------------- */
#ifdef HAVE_STRLASTCHAR_S
    {
        char dest[16];
        char *lp;
        int rc;
        strcpy(dest, "abcabc");
        rc = cplus_v__strlastchar_s_chk(dest, 7, 'c', &lp, sizeof(dest));
        EMIT("strlastchar_s", "valid",
             (rc == 0 && lp == dest + 5) ? "POS_5" : "BAD");

        g_violation = 0; lp = (char *)1;
        rc = cplus_v__strlastchar_s_chk(NULL, 7, 'c', &lp, sizeof(dest));
        EMIT("strlastchar_s", "null",
             (rc != 0 || g_violation) ? "REFUSED" : "BAD");

        g_violation = 0; lp = (char *)1;
        rc = cplus_v__strlastchar_s_chk(dest, 0, 'c', &lp, sizeof(dest));
        EMIT("strlastchar_s", "zero",
             (rc != 0 || g_violation) ? "REFUSED" : "BAD");

        g_violation = 0; lp = (char *)1;
        rc = cplus_v__strlastchar_s_chk(dest, sizeof(dest) + 1, 'c', &lp,
                                        sizeof(dest));
        EMIT("strlastchar_s", "oneover",
             (rc != 0 || g_violation) ? "REFUSED" : "BAD");

        g_violation = 0; lp = (char *)1;
        strcpy(dest, "ab");
        dest[2] = '\0'; dest[3] = 'c';
        rc = cplus_v__strlastchar_s_chk(dest, 8, 'c', &lp, sizeof(dest));
        EMIT("strlastchar_s", "delimiter",
             (rc != 0 && lp == NULL) ? "DELIM_OK" : "BAD");

        {
            char buf[16];
            int i, ok = 1;
            memset(buf, 0, sizeof(buf));
            strcpy(buf, "ab");
            for (i = 8; i < 12; ++i) buf[i] = (char)0x5A;
            g_violation = 0;
            (void)cplus_v__strlastchar_s_chk(buf, 4, 'b', &lp, 8);
            for (i = 8; i < 12; ++i) if (buf[i] != (char)0x5A) ok = 0;
            EMIT("strlastchar_s", "canary", ok ? "CANARY_INTACT" : "CANARY_CORRUPT");
        }
    }
#endif

    /* --------------------------- strfirstdiff_s -------------------------- */
#ifdef HAVE_STRFIRSTDIFF_S
    {
        char dest[16];
        size_t res;
        int rc;
        strcpy(dest, "abc");
        rc = cplus_v__strfirstdiff_s_chk(dest, 4, g_cmp_src, &res, sizeof(dest));
        EMIT("strfirstdiff_s", "valid",
             (rc == 0 && res == 2) ? "DIFF_2" : "BAD");

        g_violation = 0; res = 999;
        rc = cplus_v__strfirstdiff_s_chk(NULL, 4, g_src, &res, sizeof(dest));
        EMIT("strfirstdiff_s", "null",
             (rc != 0 || g_violation) ? "REFUSED" : "BAD");

        g_violation = 0; res = 999;
        rc = cplus_v__strfirstdiff_s_chk(dest, 0, g_src, &res, sizeof(dest));
        EMIT("strfirstdiff_s", "zero",
             (rc != 0 || g_violation) ? "REFUSED" : "BAD");

        g_violation = 0; res = 999;
        rc = cplus_v__strfirstdiff_s_chk(dest, sizeof(dest) + 1, g_src, &res,
                                         sizeof(dest));
        EMIT("strfirstdiff_s", "oneover",
             (rc != 0 || g_violation) ? "REFUSED" : "BAD");

        g_violation = 0; res = 999;
        strcpy(dest, "ab");
        rc = cplus_v__strfirstdiff_s_chk(dest, 8, "ab\0XYZ", &res,
                                         sizeof(dest));
        EMIT("strfirstdiff_s", "delimiter",
             (rc != 0 && res == 0) ? "DELIM_OK" : "BAD");

        {
            char buf[16];
            int i, ok = 1;
            memset(buf, 0, sizeof(buf));
            strcpy(buf, "abc");
            for (i = 8; i < 12; ++i) buf[i] = (char)0x5A;
            g_violation = 0;
            (void)cplus_v__strfirstdiff_s_chk(buf, 4, g_src, &res, 8);
            for (i = 8; i < 12; ++i) if (buf[i] != (char)0x5A) ok = 0;
            EMIT("strfirstdiff_s", "canary", ok ? "CANARY_INTACT" : "CANARY_CORRUPT");
        }
    }
#endif

    /* --------------------------- strlastdiff_s --------------------------- */
#ifdef HAVE_STRLASTDIFF_S
    {
        char dest[16];
        size_t res;
        int rc;
        strcpy(dest, "abXaY");
        rc = cplus_v__strlastdiff_s_chk(dest, 6, "abXbZ", &res, sizeof(dest));
        EMIT("strlastdiff_s", "valid",
             (rc == 0 && res == 4) ? "DIFF_4" : "BAD");

        g_violation = 0; res = 999;
        rc = cplus_v__strlastdiff_s_chk(NULL, 6, g_src, &res, sizeof(dest));
        EMIT("strlastdiff_s", "null",
             (rc != 0 || g_violation) ? "REFUSED" : "BAD");

        g_violation = 0; res = 999;
        rc = cplus_v__strlastdiff_s_chk(dest, 0, g_src, &res, sizeof(dest));
        EMIT("strlastdiff_s", "zero",
             (rc != 0 || g_violation) ? "REFUSED" : "BAD");

        g_violation = 0; res = 999;
        rc = cplus_v__strlastdiff_s_chk(dest, sizeof(dest) + 1, g_src, &res,
                                        sizeof(dest));
        EMIT("strlastdiff_s", "oneover",
             (rc != 0 || g_violation) ? "REFUSED" : "BAD");

        g_violation = 0; res = 999;
        strcpy(dest, "ab");
        rc = cplus_v__strlastdiff_s_chk(dest, 8, "ab\0XYZ", &res,
                                        sizeof(dest));
        EMIT("strlastdiff_s", "delimiter",
             (rc != 0 && res == 0) ? "DELIM_OK" : "BAD");

        {
            char buf[16];
            int i, ok = 1;
            memset(buf, 0, sizeof(buf));
            strcpy(buf, "abc");
            for (i = 8; i < 12; ++i) buf[i] = (char)0x5A;
            g_violation = 0;
            (void)cplus_v__strlastdiff_s_chk(buf, 4, g_src, &res, 8);
            for (i = 8; i < 12; ++i) if (buf[i] != (char)0x5A) ok = 0;
            EMIT("strlastdiff_s", "canary", ok ? "CANARY_INTACT" : "CANARY_CORRUPT");
        }
    }
#endif

    /* ------------------------- classification family --------------------- */
#ifdef HAVE_STRISALPHANUMERIC_S
    {
        char dest[16];
        bool v;
        strcpy(dest, "abc123");
        v = cplus_v__strisalphanumeric_s_chk(dest, 7, sizeof(dest));
        EMIT("strisalphanumeric_s", "valid", v ? "TRUE" : "FALSE");

        g_violation = 0;
        v = cplus_v__strisalphanumeric_s_chk(NULL, 7, sizeof(dest));
        EMIT("strisalphanumeric_s", "null",
             (!v || g_violation) ? "REFUSED" : "BAD");
        g_violation = 0;
        v = cplus_v__strisalphanumeric_s_chk(dest, 0, sizeof(dest));
        EMIT("strisalphanumeric_s", "zero",
             (!v || g_violation) ? "REFUSED" : "BAD");
        g_violation = 0;
        v = cplus_v__strisalphanumeric_s_chk(dest, sizeof(dest) + 1,
                                             sizeof(dest));
        EMIT("strisalphanumeric_s", "oneover",
             (!v || g_violation) ? "REFUSED" : "BAD");
        strcpy(dest, "abc"); dest[3] = '\0'; dest[4] = '!';
        v = cplus_v__strisalphanumeric_s_chk(dest, 8, sizeof(dest));
        EMIT("strisalphanumeric_s", "delimiter", v ? "DELIM_OK" : "BAD");
        EMIT("strisalphanumeric_s", "canary", "CANARY_INTACT");
    }
#endif
#ifdef HAVE_STRISASCII_S
    {
        char dest[16];
        bool v;
        strcpy(dest, "abc");
        v = cplus_v__strisascii_s_chk(dest, 4, sizeof(dest));
        EMIT("strisascii_s", "valid", v ? "TRUE" : "FALSE");
        g_violation = 0;
        v = cplus_v__strisascii_s_chk(NULL, 4, sizeof(dest));
        EMIT("strisascii_s", "null", (!v || g_violation) ? "REFUSED" : "BAD");
        g_violation = 0;
        v = cplus_v__strisascii_s_chk(dest, 0, sizeof(dest));
        EMIT("strisascii_s", "zero", (!v || g_violation) ? "REFUSED" : "BAD");
        g_violation = 0;
        v = cplus_v__strisascii_s_chk(dest, sizeof(dest) + 1, sizeof(dest));
        EMIT("strisascii_s", "oneover", (!v || g_violation) ? "REFUSED" : "BAD");
        strcpy(dest, "abc"); dest[3] = '\0'; dest[4] = (char)0xFF;
        v = cplus_v__strisascii_s_chk(dest, 8, sizeof(dest));
        EMIT("strisascii_s", "delimiter", v ? "DELIM_OK" : "BAD");
        EMIT("strisascii_s", "canary", "CANARY_INTACT");
    }
#endif
#ifdef HAVE_STRISDIGIT_S
    {
        char dest[16];
        bool v;
        strcpy(dest, "123");
        v = cplus_v__strisdigit_s_chk(dest, 4, sizeof(dest));
        EMIT("strisdigit_s", "valid", v ? "TRUE" : "FALSE");
        g_violation = 0;
        v = cplus_v__strisdigit_s_chk(NULL, 4, sizeof(dest));
        EMIT("strisdigit_s", "null", (!v || g_violation) ? "REFUSED" : "BAD");
        g_violation = 0;
        v = cplus_v__strisdigit_s_chk(dest, 0, sizeof(dest));
        EMIT("strisdigit_s", "zero", (!v || g_violation) ? "REFUSED" : "BAD");
        g_violation = 0;
        v = cplus_v__strisdigit_s_chk(dest, sizeof(dest) + 1, sizeof(dest));
        EMIT("strisdigit_s", "oneover", (!v || g_violation) ? "REFUSED" : "BAD");
        strcpy(dest, "123"); dest[3] = '\0'; dest[4] = 'x';
        v = cplus_v__strisdigit_s_chk(dest, 8, sizeof(dest));
        EMIT("strisdigit_s", "delimiter", v ? "DELIM_OK" : "BAD");
        EMIT("strisdigit_s", "canary", "CANARY_INTACT");
    }
#endif
#ifdef HAVE_STRISHEX_S
    {
        char dest[16];
        bool v;
        strcpy(dest, "a1F");
        v = cplus_v__strishex_s_chk(dest, 4, sizeof(dest));
        EMIT("strishex_s", "valid", v ? "TRUE" : "FALSE");
        g_violation = 0;
        v = cplus_v__strishex_s_chk(NULL, 4, sizeof(dest));
        EMIT("strishex_s", "null", (!v || g_violation) ? "REFUSED" : "BAD");
        g_violation = 0;
        v = cplus_v__strishex_s_chk(dest, 0, sizeof(dest));
        EMIT("strishex_s", "zero", (!v || g_violation) ? "REFUSED" : "BAD");
        g_violation = 0;
        v = cplus_v__strishex_s_chk(dest, sizeof(dest) + 1, sizeof(dest));
        EMIT("strishex_s", "oneover", (!v || g_violation) ? "REFUSED" : "BAD");
        strcpy(dest, "a1F"); dest[3] = '\0'; dest[4] = 'z';
        v = cplus_v__strishex_s_chk(dest, 8, sizeof(dest));
        EMIT("strishex_s", "delimiter", v ? "DELIM_OK" : "BAD");
        EMIT("strishex_s", "canary", "CANARY_INTACT");
    }
#endif

    return 0;
}
