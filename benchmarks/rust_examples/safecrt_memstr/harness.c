/* C99 harness: calls the real checked SafeCRT APIs for the same cases emitted
 * by examples.rs. One line per case: "<api> <case> <token>".
 *
 * Build (from the repo root):
 *   /usr/bin/clang -std=c99 -Ilibc/install-include -Ithird_party/safeclib/include \
 *       harness.c build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *       -lm -pthread -o harness_bin
 *
 * Diagnostics (return codes, constraint-violation counts) go to stderr; only
 * normalized tokens go to stdout so compare.sh can diff them. The canary is a
 * 0xA5 region placed immediately after the declared capacity; it must survive
 * every refused operation.
 *
 * Every symbol called below was verified present with nm -g on
 * build/safe/libcplus_safecrt.a before use (see compare.sh and NOTES.md).
 */
#include <stdio.h>
#include <stddef.h>
#include <string.h>

typedef int sc_errno_t;
typedef size_t sc_rsize_t;

/* ---- base checked APIs (safeclib-backed) ---- */
extern sc_errno_t cplus_sc_memcpy_s(void *dest, sc_rsize_t dmax, const void *src, sc_rsize_t smax);
extern sc_errno_t cplus_sc_memmove_s(void *dest, sc_rsize_t dmax, const void *src, sc_rsize_t smax);
extern sc_errno_t cplus_sc_memset_s(void *dest, sc_rsize_t dmax, int value, sc_rsize_t n);
extern sc_errno_t cplus_sc_memzero_s(void *dest, sc_rsize_t dmax);
extern sc_errno_t cplus_sc_memcmp_s(const void *dest, sc_rsize_t dmax, const void *src, sc_rsize_t smax, int *diff);
extern sc_errno_t cplus_sc_strcpy_s(char *dest, sc_rsize_t dmax, const char *src);
extern sc_errno_t cplus_sc_strncpy_s(char *dest, sc_rsize_t dmax, const char *src, sc_rsize_t slen);
extern sc_errno_t cplus_sc_strcat_s(char *dest, sc_rsize_t dmax, const char *src);
extern sc_errno_t cplus_sc_strncat_s(char *dest, sc_rsize_t dmax, const char *src, sc_rsize_t slen);
extern sc_rsize_t cplus_sc_strnlen_s(const char *str, sc_rsize_t smax);
extern char *cplus_sc_strtok_s(char *restrict dest, sc_rsize_t *restrict dmaxp, const char *restrict delim, char **restrict ptr);
extern sc_errno_t cplus_sc_strerror_s(char *dest, sc_rsize_t dmax, int errnum);
extern sc_errno_t cplus_sc_strcmp_s(const char *dest, sc_rsize_t dmax, const char *src, int *indicator);

/* ---- standard-signature auto-bounds-checked wrappers ---- */
extern void *cplus_memcpy_s_auto(void *dest, const void *src, size_t n, size_t known_dmax);
extern void *cplus_memmove_s_auto(void *dest, const void *src, size_t n, size_t known_dmax);
extern void *cplus_memset_s_auto(void *dest, int value, size_t n, size_t known_dmax);
extern int cplus_memcmp_s_auto(const void *s1, const void *s2, size_t n, size_t known_s1, size_t known_s2);
extern char *cplus_strcpy_s_auto(char *dest, const char *src, size_t known_dmax);
extern char *cplus_strncpy_s_auto(char *dest, const char *src, size_t n, size_t known_dmax);
extern char *cplus_strcat_s_auto(char *dest, const char *src, size_t known_dmax);
extern char *cplus_strncat_s_auto(char *dest, const char *src, size_t n, size_t known_dmax);

/* ---- support: suppress constraint banners, count violations, no abort ---- */
extern void cplus_sc_set_mem_constraint_handler_s_wrapper(void (*h)(const char *, void *, int));
extern void cplus_sc_set_str_constraint_handler_s_wrapper(void (*h)(const char *, void *, int));
extern void cplus_runtime_set_violation_trap(int enabled);

#define N 8
#define G 8
#define TOTAL (N + G)
#define GUARD 0xA5u

static volatile int viol;
static int g_argc;
static char **g_argv;

static void count_violation(const char *msg, void *ptr, int err) {
    (void)msg; (void)ptr; (void)err;
    viol++;
}

static int want(const char *method) {
    int i;
    if (g_argc <= 1) return 1; /* no filter: emit every API */
    for (i = 1; i < g_argc; ++i) {
        if (strcmp(g_argv[i], method) == 0) return 1;
    }
    return 0;
}

static void init_guarded(unsigned char *b, size_t cap) {
    size_t i;
    for (i = 0; i < cap; ++i) b[i] = 0x11u;
    for (; i < TOTAL; ++i) b[i] = GUARD;
}

static int canary_ok(const unsigned char *b, size_t cap) {
    size_t i;
    for (i = cap; i < TOTAL; ++i) if (b[i] != GUARD) return 0;
    return 1;
}

/* Emit the ordinary OK/REFUSED/CANARY_BROKEN token. */
static void emit(const char *m, const char *c, int refused, size_t cap,
                 const unsigned char *b) {
    int guard = canary_ok(b, cap);
    const char *tok = !guard ? "CANARY_BROKEN" : (refused ? "REFUSED" : "OK");
    fprintf(stderr, "DIAG %s %s refused=%d canary=%d\n", m, c, refused, guard);
    printf("%s %s %s\n", m, c, tok);
}

/* For cases with a NULL destination there is no canary region to inspect. */
static void emit_no_dest(const char *m, const char *c, int refused) {
    fprintf(stderr, "DIAG %s %s refused=%d canary=N/A\n", m, c, refused);
    printf("%s %s %s\n", m, c, refused ? "REFUSED" : "OK");
}

static void emit_canary(const char *m, const char *c, size_t cap,
                        const unsigned char *b) {
    printf("%s %s %s\n", m, c, canary_ok(b, cap) ? "CANARY_OK" : "CANARY_BROKEN");
}

static void emit_na(const char *m, const char *c) {
    printf("%s %s N/A\n", m, c);
}

static void fill_src(unsigned char *s) {
    int i;
    for (i = 0; i < N; ++i) s[i] = (unsigned char)(i + 1);
}

/* ============================ base memory APIs ============================ */

static void memcpy_s_cases(void) {
    const char *m = "memcpy_s";
    unsigned char b[TOTAL], s[N]; sc_errno_t rc;
    if (!want(m)) return;
    fill_src(s);
    init_guarded(b, N); rc = cplus_sc_memcpy_s(b, N, s, N);            emit(m, "valid", rc != 0, N, b);
    init_guarded(b, N); rc = cplus_sc_memcpy_s(b, N, NULL, N);         emit(m, "null", rc != 0, N, b);
    init_guarded(b, N); rc = cplus_sc_memcpy_s(b, N, s, 0);            emit(m, "zero_length", rc != 0, N, b);
    init_guarded(b, N - 1); rc = cplus_sc_memcpy_s(b, N - 1, s, N);    emit(m, "one_over", rc != 0, N - 1, b);
    init_guarded(b, N); rc = cplus_sc_memcpy_s(b, N, s, (sc_rsize_t)-1); emit(m, "overflow_size", rc != 0, N, b);
    init_guarded(b, N - 1); rc = cplus_sc_memcpy_s(b, N - 1, s, N);
    (void)rc; emit_canary(m, "canary_intact", N - 1, b);
}

static void memmove_s_cases(void) {
    const char *m = "memmove_s";
    unsigned char b[TOTAL], s[N]; sc_errno_t rc;
    if (!want(m)) return;
    fill_src(s);
    init_guarded(b, N); rc = cplus_sc_memmove_s(b, N, s, N);           emit(m, "valid", rc != 0, N, b);
    init_guarded(b, N); rc = cplus_sc_memmove_s(b, N, NULL, N);        emit(m, "null", rc != 0, N, b);
    init_guarded(b, N); rc = cplus_sc_memmove_s(b, N, s, 0);           emit(m, "zero_length", rc != 0, N, b);
    init_guarded(b, N - 1); rc = cplus_sc_memmove_s(b, N - 1, s, N);   emit(m, "one_over", rc != 0, N - 1, b);
    init_guarded(b, N); rc = cplus_sc_memmove_s(b, N, s, (sc_rsize_t)-1); emit(m, "overflow_size", rc != 0, N, b);
    init_guarded(b, N - 1); rc = cplus_sc_memmove_s(b, N - 1, s, N);
    (void)rc; emit_canary(m, "canary_intact", N - 1, b);
}

static void memset_s_cases(void) {
    const char *m = "memset_s";
    unsigned char b[TOTAL]; sc_errno_t rc;
    if (!want(m)) return;
    init_guarded(b, N); rc = cplus_sc_memset_s(b, N, 0x5A, N);         emit(m, "valid", rc != 0, N, b);
    rc = cplus_sc_memset_s(NULL, N, 0x5A, N);                          emit_no_dest(m, "null", rc != 0);
    init_guarded(b, N); rc = cplus_sc_memset_s(b, N, 0x5A, 0);         emit(m, "zero_length", rc != 0, N, b);
    init_guarded(b, N - 1); rc = cplus_sc_memset_s(b, N - 1, 0x5A, N); emit(m, "one_over", rc != 0, N - 1, b);
    init_guarded(b, N); rc = cplus_sc_memset_s(b, N, 0x5A, (sc_rsize_t)-1); emit(m, "overflow_size", rc != 0, N, b);
    init_guarded(b, N - 1); rc = cplus_sc_memset_s(b, N - 1, 0x5A, N);
    (void)rc; emit_canary(m, "canary_intact", N - 1, b);
}

static void memzero_s_cases(void) {
    const char *m = "memzero_s";
    unsigned char b[TOTAL]; sc_errno_t rc;
    if (!want(m)) return;
    init_guarded(b, N); rc = cplus_sc_memzero_s(b, N);                 emit(m, "valid", rc != 0, N, b);
    rc = cplus_sc_memzero_s(NULL, N);                                  emit_no_dest(m, "null", rc != 0);
    init_guarded(b, N); rc = cplus_sc_memzero_s(b, 0);                 emit(m, "zero_length", rc != 0, N, b);
    emit_na(m, "one_over"); /* no separate requested length */
    init_guarded(b, N); rc = cplus_sc_memzero_s(b, (sc_rsize_t)-1);    emit(m, "overflow_size", rc != 0, N, b);
    init_guarded(b, N); rc = cplus_sc_memzero_s(b, N);
    (void)rc; emit_canary(m, "canary_intact", N, b);
}

static void memcmp_s_cases(void) {
    const char *m = "memcmp_s";
    unsigned char b[TOTAL], s[N]; sc_errno_t rc; int diff; int i;
    if (!want(m)) return;
    fill_src(s);
    init_guarded(b, N); for (i = 0; i < N; ++i) b[i] = s[i];
    rc = cplus_sc_memcmp_s(b, N, s, N, &diff);                         emit(m, "valid", rc != 0, N, b);
    init_guarded(b, N); rc = cplus_sc_memcmp_s(b, N, NULL, N, &diff);  emit(m, "null", rc != 0, N, b);
    init_guarded(b, N); rc = cplus_sc_memcmp_s(b, N, s, 0, &diff);     emit(m, "zero_length", rc != 0, N, b);
    init_guarded(b, N - 1); rc = cplus_sc_memcmp_s(b, N - 1, s, N, &diff); emit(m, "one_over", rc != 0, N - 1, b);
    init_guarded(b, N); rc = cplus_sc_memcmp_s(b, N, s, (sc_rsize_t)-1, &diff); emit(m, "overflow_size", rc != 0, N, b);
    init_guarded(b, N - 1); rc = cplus_sc_memcmp_s(b, N - 1, s, N, &diff);
    (void)rc; emit_canary(m, "canary_intact", N - 1, b);
}

/* ============================ base string APIs ============================ */

static void strcpy_s_cases(void) {
    const char *m = "strcpy_s";
    unsigned char b[TOTAL]; sc_errno_t rc;
    if (!want(m)) return;
    init_guarded(b, N); rc = cplus_sc_strcpy_s((char *)b, N, "abc");   emit(m, "valid", rc != 0, N, b);
    init_guarded(b, N); rc = cplus_sc_strcpy_s((char *)b, N, NULL);    emit(m, "null", rc != 0, N, b);
    init_guarded(b, N); rc = cplus_sc_strcpy_s((char *)b, 0, "abc");   emit(m, "zero_length", rc != 0, N, b);
    init_guarded(b, 3); rc = cplus_sc_strcpy_s((char *)b, 3, "abc");   emit(m, "one_over", rc != 0, 3, b);
    init_guarded(b, N); rc = cplus_sc_strcpy_s((char *)b, N, "ABCDEFGHIJKL"); emit(m, "overflow_size", rc != 0, N, b);
    init_guarded(b, 3); rc = cplus_sc_strcpy_s((char *)b, 3, "abc");
    (void)rc; emit_canary(m, "canary_intact", 3, b);
}

static void strncpy_s_cases(void) {
    const char *m = "strncpy_s";
    unsigned char b[TOTAL]; sc_errno_t rc;
    if (!want(m)) return;
    init_guarded(b, N); rc = cplus_sc_strncpy_s((char *)b, N, "abc", 4); emit(m, "valid", rc != 0, N, b);
    init_guarded(b, N); rc = cplus_sc_strncpy_s((char *)b, N, NULL, 4);  emit(m, "null", rc != 0, N, b);
    init_guarded(b, N); rc = cplus_sc_strncpy_s((char *)b, N, "abc", 0); emit(m, "zero_length", rc != 0, N, b);
    init_guarded(b, 3); rc = cplus_sc_strncpy_s((char *)b, 3, "abc", 4); emit(m, "one_over", rc != 0, 3, b);
    init_guarded(b, N); rc = cplus_sc_strncpy_s((char *)b, N, "abc", (sc_rsize_t)-1); emit(m, "overflow_size", rc != 0, N, b);
    init_guarded(b, 3); rc = cplus_sc_strncpy_s((char *)b, 3, "abc", 4);
    (void)rc; emit_canary(m, "canary_intact", 3, b);
}

static void strcat_s_cases(void) {
    const char *m = "strcat_s";
    unsigned char b[TOTAL]; sc_errno_t rc;
    if (!want(m)) return;
    init_guarded(b, N); ((char *)b)[0] = 'x'; ((char *)b)[1] = '\0';
    rc = cplus_sc_strcat_s((char *)b, N, "abc");                       emit(m, "valid", rc != 0, N, b);
    init_guarded(b, N); ((char *)b)[0] = '\0';
    rc = cplus_sc_strcat_s((char *)b, N, NULL);                        emit(m, "null", rc != 0, N, b);
    init_guarded(b, N); ((char *)b)[0] = '\0';
    rc = cplus_sc_strcat_s((char *)b, 0, "abc");                       emit(m, "zero_length", rc != 0, N, b);
    init_guarded(b, 4); ((char *)b)[0] = 'x'; ((char *)b)[1] = '\0';
    rc = cplus_sc_strcat_s((char *)b, 4, "abc");                       emit(m, "one_over", rc != 0, 4, b);
    init_guarded(b, N); ((char *)b)[0] = 'x'; ((char *)b)[1] = '\0';
    rc = cplus_sc_strcat_s((char *)b, N, "ABCDEFGHIJKL");              emit(m, "overflow_size", rc != 0, N, b);
    init_guarded(b, 4); ((char *)b)[0] = 'x'; ((char *)b)[1] = '\0';
    rc = cplus_sc_strcat_s((char *)b, 4, "abc");
    (void)rc; emit_canary(m, "canary_intact", 4, b);
}

static void strncat_s_cases(void) {
    const char *m = "strncat_s";
    unsigned char b[TOTAL]; sc_errno_t rc;
    if (!want(m)) return;
    init_guarded(b, N); ((char *)b)[0] = 'x'; ((char *)b)[1] = '\0';
    rc = cplus_sc_strncat_s((char *)b, N, "abc", 3);                   emit(m, "valid", rc != 0, N, b);
    init_guarded(b, N); ((char *)b)[0] = '\0';
    rc = cplus_sc_strncat_s((char *)b, N, NULL, 3);                    emit(m, "null", rc != 0, N, b);
    init_guarded(b, N); ((char *)b)[0] = '\0';
    rc = cplus_sc_strncat_s((char *)b, 0, "abc", 3);                   emit(m, "zero_length", rc != 0, N, b);
    init_guarded(b, 4); ((char *)b)[0] = 'x'; ((char *)b)[1] = '\0';
    rc = cplus_sc_strncat_s((char *)b, 4, "abc", 4);                   emit(m, "one_over", rc != 0, 4, b);
    init_guarded(b, N); ((char *)b)[0] = 'x'; ((char *)b)[1] = '\0';
    rc = cplus_sc_strncat_s((char *)b, N, "abc", (sc_rsize_t)-1);      emit(m, "overflow_size", rc != 0, N, b);
    init_guarded(b, 4); ((char *)b)[0] = 'x'; ((char *)b)[1] = '\0';
    rc = cplus_sc_strncat_s((char *)b, 4, "abc", 4);
    (void)rc; emit_canary(m, "canary_intact", 4, b);
}

static void strnlen_s_cases(void) {
    const char *m = "strnlen_s";
    unsigned char b[TOTAL]; sc_rsize_t got;
    if (!want(m)) return;
    init_guarded(b, N); memcpy(b, "abc", 4); viol = 0; got = cplus_sc_strnlen_s((char *)b, N);
    emit(m, "valid", viol != 0 || got != 3, N, b);
    init_guarded(b, N); memcpy(b, "abc", 4); viol = 0; got = cplus_sc_strnlen_s(NULL, N);
    emit(m, "null", viol != 0, N, b); (void)got;
    init_guarded(b, N); memcpy(b, "abc", 4); viol = 0; got = cplus_sc_strnlen_s((char *)b, 0);
    emit(m, "zero_length", viol != 0, N, b); (void)got;
    init_guarded(b, N); memcpy(b, "abc", 4); viol = 0; got = cplus_sc_strnlen_s((char *)b, 3);
    emit(m, "one_over", viol != 0 || got != 3, N, b);
    init_guarded(b, N); memcpy(b, "abc", 4); viol = 0; got = cplus_sc_strnlen_s((char *)b, (sc_rsize_t)-1);
    emit(m, "overflow_size", viol != 0, N, b); (void)got;
    init_guarded(b, N); memcpy(b, "abc", 4);
    emit_canary(m, "canary_intact", N, b);
}

static void strtok_s_cases(void) {
    const char *m = "strtok_s";
    unsigned char b[TOTAL]; sc_rsize_t dmax; char *ctx; char *tok;
    if (!want(m)) return;
    init_guarded(b, N); memcpy(b, "a,b,c", 6); dmax = N; ctx = NULL;
    tok = cplus_sc_strtok_s((char *)b, &dmax, ",", &ctx);              emit(m, "valid", tok == NULL, N, b);
    init_guarded(b, N); memcpy(b, "a,b,c", 6); dmax = N; ctx = NULL;
    tok = cplus_sc_strtok_s(NULL, &dmax, ",", &ctx);                   emit(m, "null", tok == NULL, N, b);
    init_guarded(b, N); memcpy(b, "a,b,c", 6); dmax = 0; ctx = NULL;
    tok = cplus_sc_strtok_s((char *)b, &dmax, ",", &ctx);              emit(m, "zero_length", tok == NULL, N, b);
    init_guarded(b, N); memcpy(b, "a,b,c", 6); dmax = N - 1; ctx = NULL;
    tok = cplus_sc_strtok_s((char *)b, &dmax, ",", &ctx);              emit(m, "one_over", tok == NULL, N, b);
    init_guarded(b, N); memcpy(b, "a,b,c", 6); dmax = (sc_rsize_t)-1; ctx = NULL;
    tok = cplus_sc_strtok_s((char *)b, &dmax, ",", &ctx);              emit(m, "overflow_size", tok == NULL, N, b);
    init_guarded(b, N); memcpy(b, "a,b,c", 6); dmax = N; ctx = NULL;
    tok = cplus_sc_strtok_s((char *)b, &dmax, ",", &ctx); (void)tok;
    emit_canary(m, "canary_intact", N, b);
}

static void strerror_s_cases(void) {
    const char *m = "strerror_s";
    unsigned char b[TOTAL]; sc_errno_t rc;
    if (!want(m)) return;
    init_guarded(b, N); rc = cplus_sc_strerror_s((char *)b, N, 2);     emit(m, "valid", rc != 0, N, b);
    rc = cplus_sc_strerror_s(NULL, N, 2);                              emit_no_dest(m, "null", rc != 0);
    init_guarded(b, N); rc = cplus_sc_strerror_s((char *)b, 0, 2);     emit(m, "zero_length", rc != 0, N, b);
    init_guarded(b, 3); rc = cplus_sc_strerror_s((char *)b, 3, 2);     emit(m, "one_over", rc != 0, 3, b);
    init_guarded(b, N); rc = cplus_sc_strerror_s((char *)b, (sc_rsize_t)-1, 2); emit(m, "overflow_size", rc != 0, N, b);
    init_guarded(b, N); rc = cplus_sc_strerror_s((char *)b, N, 2);
    (void)rc; emit_canary(m, "canary_intact", N, b);
}

static void strcmp_s_cases(void) {
    const char *m = "strcmp_s";
    unsigned char b[TOTAL]; sc_errno_t rc; int diff;
    if (!want(m)) return;
    init_guarded(b, N); memcpy(b, "abc", 4);
    rc = cplus_sc_strcmp_s((char *)b, N, "abc", &diff);                emit(m, "valid", rc != 0, N, b);
    init_guarded(b, N); memcpy(b, "abc", 4);
    rc = cplus_sc_strcmp_s((char *)b, N, NULL, &diff);                 emit(m, "null", rc != 0, N, b);
    init_guarded(b, N); memcpy(b, "abc", 4);
    rc = cplus_sc_strcmp_s((char *)b, 0, "abc", &diff);                emit(m, "zero_length", rc != 0, N, b);
    init_guarded(b, 3); b[0] = 'a'; b[1] = 'b'; b[2] = '\0';
    rc = cplus_sc_strcmp_s((char *)b, 3, "ABCDEFGHIJKL", &diff);       emit(m, "one_over", rc != 0, 3, b);
    init_guarded(b, N); memcpy(b, "abc", 4);
    rc = cplus_sc_strcmp_s((char *)b, (sc_rsize_t)-1, "abc", &diff);   emit(m, "overflow_size", rc != 0, N, b);
    init_guarded(b, N); memcpy(b, "abc", 4);
    rc = cplus_sc_strcmp_s((char *)b, N, "abc", &diff);
    (void)rc; (void)diff; emit_canary(m, "canary_intact", N, b);
}

/* =============================== auto variants ============================= */

static void memcpy_s_auto_cases(void) {
    const char *m = "memcpy_s_auto";
    unsigned char b[TOTAL], s[N]; void *p;
    if (!want(m)) return;
    fill_src(s);
    init_guarded(b, N); p = cplus_memcpy_s_auto(b, s, N, N);           emit(m, "valid", p == NULL, N, b);
    init_guarded(b, N); p = cplus_memcpy_s_auto(b, NULL, N, N);        emit(m, "null", p == NULL, N, b);
    init_guarded(b, N); p = cplus_memcpy_s_auto(b, s, 0, N);           emit(m, "zero_length", p == NULL, N, b);
    init_guarded(b, N); p = cplus_memcpy_s_auto(b, s, N + 1, N);       emit(m, "one_over", p == NULL, N, b);
    init_guarded(b, N); p = cplus_memcpy_s_auto(b, s, (size_t)-1, N);  emit(m, "overflow_size", p == NULL, N, b);
    init_guarded(b, N); p = cplus_memcpy_s_auto(b, s, N + 1, N);
    (void)p; emit_canary(m, "canary_intact", N, b);
}

static void memmove_s_auto_cases(void) {
    const char *m = "memmove_s_auto";
    unsigned char b[TOTAL], s[N]; void *p;
    if (!want(m)) return;
    fill_src(s);
    init_guarded(b, N); p = cplus_memmove_s_auto(b, s, N, N);          emit(m, "valid", p == NULL, N, b);
    init_guarded(b, N); p = cplus_memmove_s_auto(b, NULL, N, N);       emit(m, "null", p == NULL, N, b);
    init_guarded(b, N); p = cplus_memmove_s_auto(b, s, 0, N);          emit(m, "zero_length", p == NULL, N, b);
    init_guarded(b, N); p = cplus_memmove_s_auto(b, s, N + 1, N);      emit(m, "one_over", p == NULL, N, b);
    init_guarded(b, N); p = cplus_memmove_s_auto(b, s, (size_t)-1, N); emit(m, "overflow_size", p == NULL, N, b);
    init_guarded(b, N); p = cplus_memmove_s_auto(b, s, N + 1, N);
    (void)p; emit_canary(m, "canary_intact", N, b);
}

static void memset_s_auto_cases(void) {
    const char *m = "memset_s_auto";
    unsigned char b[TOTAL]; void *p;
    if (!want(m)) return;
    init_guarded(b, N); p = cplus_memset_s_auto(b, 0x5A, N, N);        emit(m, "valid", p == NULL, N, b);
    p = cplus_memset_s_auto(NULL, 0x5A, N, N);                         emit_no_dest(m, "null", p == NULL);
    init_guarded(b, N); p = cplus_memset_s_auto(b, 0x5A, 0, N);        emit(m, "zero_length", p == NULL, N, b);
    init_guarded(b, N); p = cplus_memset_s_auto(b, 0x5A, N + 1, N);    emit(m, "one_over", p == NULL, N, b);
    init_guarded(b, N); p = cplus_memset_s_auto(b, 0x5A, (size_t)-1, N); emit(m, "overflow_size", p == NULL, N, b);
    init_guarded(b, N); p = cplus_memset_s_auto(b, 0x5A, N + 1, N);
    (void)p; emit_canary(m, "canary_intact", N, b);
}

static void memcmp_s_auto_cases(void) {
    const char *m = "memcmp_s_auto";
    unsigned char b[TOTAL], s[N]; int r; int i;
    if (!want(m)) return;
    fill_src(s);
    init_guarded(b, N); for (i = 0; i < N; ++i) b[i] = s[i];
    r = cplus_memcmp_s_auto(b, s, N, N, N);                            emit(m, "valid", r != 0, N, b);
    init_guarded(b, N); r = cplus_memcmp_s_auto(b, NULL, N, N, N);     emit(m, "null", r != 0, N, b);
    init_guarded(b, N); r = cplus_memcmp_s_auto(b, s, 0, N, N);        emit(m, "zero_length", r != 0, N, b);
    init_guarded(b, N); r = cplus_memcmp_s_auto(b, s, N + 1, N, N);    emit(m, "one_over", r != 0, N, b);
    init_guarded(b, N); r = cplus_memcmp_s_auto(b, s, (size_t)-1, N, N); emit(m, "overflow_size", r != 0, N, b);
    init_guarded(b, N); r = cplus_memcmp_s_auto(b, s, N + 1, N, N);
    (void)r; emit_canary(m, "canary_intact", N, b);
}

static void strcpy_s_auto_cases(void) {
    const char *m = "strcpy_s_auto";
    unsigned char b[TOTAL]; char *p;
    if (!want(m)) return;
    init_guarded(b, N); p = cplus_strcpy_s_auto((char *)b, "abc", N);  emit(m, "valid", p == NULL, N, b);
    init_guarded(b, N); p = cplus_strcpy_s_auto((char *)b, NULL, N);   emit(m, "null", p == NULL, N, b);
    init_guarded(b, N); p = cplus_strcpy_s_auto((char *)b, "", N);     emit(m, "zero_length", p == NULL, N, b);
    init_guarded(b, 3); p = cplus_strcpy_s_auto((char *)b, "abc", 3);  emit(m, "one_over", p == NULL, 3, b);
    init_guarded(b, N); p = cplus_strcpy_s_auto((char *)b, "ABCDEFGHIJKL", N); emit(m, "overflow_size", p == NULL, N, b);
    init_guarded(b, 3); p = cplus_strcpy_s_auto((char *)b, "abc", 3);
    (void)p; emit_canary(m, "canary_intact", 3, b);
}

static void strncpy_s_auto_cases(void) {
    const char *m = "strncpy_s_auto";
    unsigned char b[TOTAL]; char *p;
    if (!want(m)) return;
    init_guarded(b, N); p = cplus_strncpy_s_auto((char *)b, "abc", 4, N); emit(m, "valid", p == NULL, N, b);
    init_guarded(b, N); p = cplus_strncpy_s_auto((char *)b, NULL, 4, N);  emit(m, "null", p == NULL, N, b);
    init_guarded(b, N); p = cplus_strncpy_s_auto((char *)b, "abc", 0, N); emit(m, "zero_length", p == NULL, N, b);
    init_guarded(b, 3); p = cplus_strncpy_s_auto((char *)b, "abc", 4, 3); emit(m, "one_over", p == NULL, 3, b);
    init_guarded(b, N); p = cplus_strncpy_s_auto((char *)b, "abc", (size_t)-1, N); emit(m, "overflow_size", p == NULL, N, b);
    init_guarded(b, 3); p = cplus_strncpy_s_auto((char *)b, "abc", 4, 3);
    (void)p; emit_canary(m, "canary_intact", 3, b);
}

static void strcat_s_auto_cases(void) {
    const char *m = "strcat_s_auto";
    unsigned char b[TOTAL]; char *p;
    if (!want(m)) return;
    init_guarded(b, N); ((char *)b)[0] = 'x'; ((char *)b)[1] = '\0';
    p = cplus_strcat_s_auto((char *)b, "abc", N);                      emit(m, "valid", p == NULL, N, b);
    init_guarded(b, N); ((char *)b)[0] = 'x'; ((char *)b)[1] = '\0';
    p = cplus_strcat_s_auto((char *)b, NULL, N);                       emit(m, "null", p == NULL, N, b);
    init_guarded(b, N); ((char *)b)[0] = '\0';
    p = cplus_strcat_s_auto((char *)b, "", N);                         emit(m, "zero_length", p == NULL, N, b);
    init_guarded(b, 4); ((char *)b)[0] = 'x'; ((char *)b)[1] = '\0';
    p = cplus_strcat_s_auto((char *)b, "abc", 4);                      emit(m, "one_over", p == NULL, 4, b);
    init_guarded(b, N); ((char *)b)[0] = 'x'; ((char *)b)[1] = '\0';
    p = cplus_strcat_s_auto((char *)b, "ABCDEFGHIJKL", N);             emit(m, "overflow_size", p == NULL, N, b);
    init_guarded(b, 4); ((char *)b)[0] = 'x'; ((char *)b)[1] = '\0';
    p = cplus_strcat_s_auto((char *)b, "abc", 4);
    (void)p; emit_canary(m, "canary_intact", 4, b);
}

static void strncat_s_auto_cases(void) {
    const char *m = "strncat_s_auto";
    unsigned char b[TOTAL]; char *p;
    if (!want(m)) return;
    init_guarded(b, N); ((char *)b)[0] = 'x'; ((char *)b)[1] = '\0';
    p = cplus_strncat_s_auto((char *)b, "abc", 3, N);                  emit(m, "valid", p == NULL, N, b);
    init_guarded(b, N); ((char *)b)[0] = 'x'; ((char *)b)[1] = '\0';
    p = cplus_strncat_s_auto((char *)b, NULL, 3, N);                   emit(m, "null", p == NULL, N, b);
    init_guarded(b, N); ((char *)b)[0] = '\0';
    p = cplus_strncat_s_auto((char *)b, "abc", 0, N);                  emit(m, "zero_length", p == NULL, N, b);
    init_guarded(b, 4); ((char *)b)[0] = 'x'; ((char *)b)[1] = '\0';
    p = cplus_strncat_s_auto((char *)b, "abc", 4, 4);                  emit(m, "one_over", p == NULL, 4, b);
    init_guarded(b, N); ((char *)b)[0] = 'x'; ((char *)b)[1] = '\0';
    p = cplus_strncat_s_auto((char *)b, "abc", (size_t)-1, N);         emit(m, "overflow_size", p == NULL, N, b);
    init_guarded(b, 4); ((char *)b)[0] = 'x'; ((char *)b)[1] = '\0';
    p = cplus_strncat_s_auto((char *)b, "abc", 4, 4);
    (void)p; emit_canary(m, "canary_intact", 4, b);
}

int main(int argc, char **argv) {
    g_argc = argc;
    g_argv = argv;

    /* Keep refusals observable and non-fatal; count constraint violations. */
    cplus_runtime_set_violation_trap(0);
    cplus_sc_set_mem_constraint_handler_s_wrapper(count_violation);
    cplus_sc_set_str_constraint_handler_s_wrapper(count_violation);

    memcpy_s_cases();
    memmove_s_cases();
    memset_s_cases();
    memzero_s_cases();
    memcmp_s_cases();
    strcpy_s_cases();
    strncpy_s_cases();
    strcat_s_cases();
    strncat_s_cases();
    strnlen_s_cases();
    strtok_s_cases();
    strerror_s_cases();
    strcmp_s_cases();
    memcpy_s_auto_cases();
    memmove_s_auto_cases();
    memset_s_auto_cases();
    memcmp_s_auto_cases();
    strcpy_s_auto_cases();
    strncpy_s_auto_cases();
    strcat_s_auto_cases();
    strncat_s_auto_cases();
    return 0;
}
