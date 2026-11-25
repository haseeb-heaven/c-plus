/* C99 harness: ORDINARY ISO C spellings remapped by -include
 * stdlib/safe/safe_c.h to the C+ checked layer.
 *
 * Build (from the repo root):
 *   /usr/bin/clang -std=c99 -Wall -Wextra -Werror -I. \
 *     -Ilibc/install-include -Ithird_party/safeclib/include \
 *     -include stdlib/safe/safe_c.h \
 *     benchmarks/rust_examples/stdc_compare/harness.c \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -lm -pthread -o out/harness_bin
 *
 * Every call below is written with the ordinary C name (memcpy, strcpy,
 * malloc, free, ...). The force-included safe_c.h rewrites each one to the
 * checked entry point verified by compare.sh with nm:
 *   memcpy   -> cplus_memcpy_s_auto     malloc -> malloc_s
 *   memmove  -> cplus_memmove_s_auto    calloc -> calloc_s
 *   memset   -> cplus_memset_s_auto     realloc-> realloc_s
 *   memcmp   -> cplus_memcmp_s_auto     free   -> free_s
 *   strcpy   -> cplus_strcpy_s_auto
 *   strncpy  -> cplus_strncpy_s_auto
 *   strcat   -> cplus_strcat_s_auto
 *   strncat  -> cplus_strncat_s_auto
 *
 * Only normalized tokens go to stdout ("<api> <case> <token>"); diagnostics
 * go to stderr. The canary is a SEPARATE 0xA5 object so that __builtin_object_size
 * of each destination equals the declared capacity: an over-sized request is
 * refused before the guard could ever be reached.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>

/* Support hooks not declared by the installed headers. */
extern void cplus_runtime_set_violation_trap(int enabled);
extern void cplus_sc_set_mem_constraint_handler_s_wrapper(void (*h)(const char *, void *, int));
extern void cplus_sc_set_str_constraint_handler_s_wrapper(void (*h)(const char *, void *, int));

#define N 8
#define G 8
#define GUARD 0xA5u
#define LONG_STR "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"

static unsigned char g_guard[G];
static volatile int g_viol;
static int g_argc;
static char **g_argv;

static void count_violation(const char *msg, void *ptr, int err) {
    (void)msg; (void)ptr; (void)err;
    g_viol++;
}

static int want(const char *api) {
    int i;
    if (g_argc <= 1) return 1;
    for (i = 1; i < g_argc; ++i) {
        if (strcmp(g_argv[i], api) == 0) return 1;
    }
    return 0;
}

static void guard_init(void) {
    int i;
    for (i = 0; i < G; ++i) g_guard[i] = (unsigned char)GUARD;
}

static int guard_intact(void) {
    int i;
    for (i = 0; i < G; ++i) if (g_guard[i] != (unsigned char)GUARD) return 0;
    return 1;
}

static void emit(const char *api, const char *c, int refused) {
    const char *tok = !guard_intact() ? "CANARY_BROKEN" : (refused ? "REFUSED" : "OK");
    fprintf(stderr, "DIAG %s %s refused=%d guard=%d viol=%d\n",
            api, c, refused, guard_intact(), g_viol);
    printf("%s %s %s\n", api, c, tok);
}

static void emit_na(const char *api, const char *c) {
    printf("%s %s N/A\n", api, c);
}

static void emit_canary(const char *api, const char *c) {
    printf("%s %s %s\n", api, c,
           guard_intact() ? "CANARY_OK" : "CANARY_BROKEN");
}

/* ---------------------------- memory APIs ------------------------------- */

static void mem_copy_cases(const char *api, int is_move) {
    unsigned char d[N], small[N - 1], s[N];
    int i;
    if (!want(api)) return;
    for (i = 0; i < N; ++i) s[i] = (unsigned char)(i + 1);

    guard_init();
    for (i = 0; i < N; ++i) d[i] = 0;
    if (is_move) { (void)memmove(d, s, N); } else { (void)memcpy(d, s, N); }
    emit(api, "valid", 0);

    guard_init();
    if (is_move) { (void)memmove(NULL, s, N); } else { (void)memcpy(NULL, s, N); }
    emit(api, "null", 1);

    guard_init();
    if (is_move) { (void)memmove(d, s, 0); } else { (void)memcpy(d, s, 0); }
    emit(api, "zero_length", 0);

    guard_init();
    if (is_move) { (void)memmove(small, s, N); } else { (void)memcpy(small, s, N); }
    emit(api, "one_over", 1);

    guard_init();
    if (is_move) { (void)memmove(d, s, (size_t)-1); } else { (void)memcpy(d, s, (size_t)-1); }
    emit(api, "huge_n", 1);

    guard_init();
    if (is_move) { (void)memmove(d, s, N); } else { (void)memcpy(d, s, N); }
    emit_canary(api, "canary");
}

static void memset_cases(void) {
    const char *api = "memset";
    unsigned char d[N], small[N - 1];
    if (!want(api)) return;

    guard_init();
    (void)memset(d, 0x5A, N);
    emit(api, "valid", 0);

    guard_init();
    (void)memset(NULL, 0x5A, N);
    emit(api, "null", 1);

    guard_init();
    (void)memset(d, 0x5A, 0);
    emit(api, "zero_length", 0);

    guard_init();
    (void)memset(small, 0x5A, N);
    emit(api, "one_over", 1);

    guard_init();
    (void)memset(d, 0x5A, (size_t)-1);
    emit(api, "huge_n", 1);

    guard_init();
    (void)memset(d, 0x5A, N);
    emit_canary(api, "canary");
}

static void memcmp_cases(void) {
    const char *api = "memcmp";
    unsigned char d[N], small[N - 1], s[N];
    int i, rc;
    if (!want(api)) return;
    for (i = 0; i < N; ++i) { s[i] = (unsigned char)(i + 1); d[i] = s[i]; }

    guard_init();
    rc = memcmp(d, s, N);
    emit(api, "valid", rc != 0);

    guard_init();
    rc = memcmp(NULL, s, N);
    emit(api, "null", rc != 0);

    guard_init();
    rc = memcmp(d, s, 0);
    emit(api, "zero_length", rc != 0);

    guard_init();
    rc = memcmp(small, s, N);
    emit(api, "one_over", rc != 0);

    guard_init();
    rc = memcmp(d, s, (size_t)-1);
    emit(api, "huge_n", rc != 0);

    guard_init();
    rc = memcmp(d, s, N);
    (void)rc;
    emit_canary(api, "canary");
}

/* ---------------------------- string APIs ------------------------------- */

static void strcpy_cases(void) {
    const char *api = "strcpy";
    char d[N], small[3];
    if (!want(api)) return;

    guard_init();
    (void)strcpy(d, "abc");
    emit(api, "valid", 0);

    guard_init();
    (void)strcpy(NULL, "abc");
    emit(api, "null", 1);

    guard_init();
    (void)strcpy(d, "");
    emit(api, "zero_length", 0);

    guard_init();
    (void)strcpy(small, "abc");
    emit(api, "one_over", 1);

    guard_init();
    (void)strcpy(d, LONG_STR);
    emit(api, "huge_n", 1);

    guard_init();
    (void)strcpy(d, "abc");
    emit_canary(api, "canary");
}

static void strncpy_cases(void) {
    const char *api = "strncpy";
    char d[N], small[3];
    if (!want(api)) return;

    guard_init();
    (void)strncpy(d, "abc", 4);
    emit(api, "valid", 0);

    guard_init();
    (void)strncpy(NULL, "abc", 4);
    emit(api, "null", 1);

    guard_init();
    (void)strncpy(d, "abc", 0);
    emit(api, "zero_length", 0);

    guard_init();
    (void)strncpy(small, "abc", 4);
    emit(api, "one_over", 1);

    guard_init();
    (void)strncpy(d, "abc", (size_t)-1);
    emit(api, "huge_n", 1);

    guard_init();
    (void)strncpy(d, "abc", 4);
    emit_canary(api, "canary");
}

static void strcat_cases(void) {
    const char *api = "strcat";
    char d[N], small[4];
    if (!want(api)) return;

    guard_init();
    d[0] = 'x'; d[1] = '\0';
    (void)strcat(d, "abc");
    emit(api, "valid", 0);

    guard_init();
    (void)strcat(NULL, "abc");
    emit(api, "null", 1);

    guard_init();
    d[0] = 'x'; d[1] = '\0';
    (void)strcat(d, "");
    emit(api, "zero_length", 0);

    guard_init();
    small[0] = 'x'; small[1] = '\0';
    (void)strcat(small, "abc");
    emit(api, "one_over", 1);

    guard_init();
    d[0] = 'x'; d[1] = '\0';
    (void)strcat(d, LONG_STR);
    emit(api, "huge_n", 1);

    guard_init();
    d[0] = 'x'; d[1] = '\0';
    (void)strcat(d, "abc");
    emit_canary(api, "canary");
}

static void strncat_cases(void) {
    const char *api = "strncat";
    char d[N], small[4];
    if (!want(api)) return;

    guard_init();
    d[0] = 'x'; d[1] = '\0';
    (void)strncat(d, "abc", 3);
    emit(api, "valid", 0);

    guard_init();
    (void)strncat(NULL, "abc", 3);
    emit(api, "null", 1);

    guard_init();
    d[0] = 'x'; d[1] = '\0';
    (void)strncat(d, "abc", 0);
    emit(api, "zero_length", 0);

    guard_init();
    small[0] = 'x'; small[1] = '\0';
    (void)strncat(small, "abc", 4);
    emit(api, "one_over", 1);

    guard_init();
    d[0] = 'x'; d[1] = '\0';
    (void)strncat(d, "abc", (size_t)-1);
    emit(api, "huge_n", 1);

    guard_init();
    d[0] = 'x'; d[1] = '\0';
    (void)strncat(d, "abc", 3);
    emit_canary(api, "canary");
}

/* --------------------------- allocator APIs ----------------------------- */

static void forced_fail_begin(void) { cplus_safe_alloc_fail_after(0); }
static void forced_fail_end(void)   { cplus_safe_alloc_fail_reset(); }

static void malloc_cases(void) {
    const char *api = "malloc";
    void *p, *q;
    if (!want(api)) return;

    guard_init();
    p = malloc(N);
    emit(api, "valid", p == NULL);
    free(p);

    guard_init();
    forced_fail_begin();
    q = malloc(N);
    forced_fail_end();
    emit(api, "null", q == NULL);
    free(q);

    guard_init();
    p = malloc(0);
    emit(api, "zero_length", p == NULL);
    free(p);

    emit_na(api, "one_over");

    guard_init();
    p = malloc((size_t)-1);
    emit(api, "huge_n", p == NULL);
    free(p);

    guard_init();
    p = malloc(N);
    emit_canary(api, "canary");
    free(p);
}

static void calloc_cases(void) {
    const char *api = "calloc";
    void *p, *q;
    if (!want(api)) return;

    guard_init();
    p = calloc(N, 1);
    emit(api, "valid", p == NULL);
    free(p);

    guard_init();
    forced_fail_begin();
    q = calloc(N, 1);
    forced_fail_end();
    emit(api, "null", q == NULL);
    free(q);

    guard_init();
    p = calloc(0, 1);
    emit(api, "zero_length", p == NULL);
    free(p);

    emit_na(api, "one_over");

    guard_init();
    p = calloc((size_t)-1, 2);
    emit(api, "huge_n", p == NULL);
    free(p);

    guard_init();
    p = calloc(N, 1);
    emit_canary(api, "canary");
    free(p);
}

static void realloc_cases(void) {
    const char *api = "realloc";
    void *p, *q;
    if (!want(api)) return;

    guard_init();
    p = malloc(N);
    q = realloc(p, N * 2);
    emit(api, "valid", q == NULL);
    free(q);

    guard_init();
    p = malloc(N);
    forced_fail_begin();
    q = realloc(p, N * 2);
    forced_fail_end();
    emit(api, "null", q == NULL);
    free(p); /* injected failure leaves the original block live */
    free(q);

    guard_init();
    p = malloc(N);
    q = realloc(p, 0);
    emit(api, "zero_length", q == NULL);
    free(q);

    emit_na(api, "one_over");

    guard_init();
    p = malloc(N);
    q = realloc(p, (size_t)-1);
    emit(api, "huge_n", q == NULL);
    free(p);
    free(q);

    guard_init();
    p = malloc(N);
    q = realloc(p, N);
    emit_canary(api, "canary");
    free(q);
}

static void free_cases(void) {
    const char *api = "free";
    void *p;
    if (!want(api)) return;

    guard_init();
    p = malloc(N);
    free(p);
    emit(api, "valid", cplus_safe_ptr_is_live(p));

    guard_init();
    free(NULL);
    emit(api, "null", 0);

    emit_na(api, "zero_length");
    emit_na(api, "one_over");
    emit_na(api, "huge_n");

    guard_init();
    p = malloc(N);
    free(p);
    emit_canary(api, "canary");
}

int main(int argc, char **argv) {
    g_argc = argc;
    g_argv = argv;

    /* Keep refusals observable and non-fatal: count them, never abort. */
    cplus_runtime_set_violation_trap(0);
    cplus_sc_set_mem_constraint_handler_s_wrapper(count_violation);
    cplus_sc_set_str_constraint_handler_s_wrapper(count_violation);

    mem_copy_cases("memcpy", 0);
    mem_copy_cases("memmove", 1);
    memset_cases();
    memcmp_cases();
    strcpy_cases();
    strncpy_cases();
    strcat_cases();
    strncat_cases();
    malloc_cases();
    calloc_cases();
    realloc_cases();
    free_cases();
    return 0;
}
