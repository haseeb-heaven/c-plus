/* stdc_safety hostile-input harness.
 *
 * The SAME source is compiled two ways by run.sh:
 *
 *   CHECKED: /usr/bin/clang -std=c99 ... -include stdlib/safe/safe_c.h
 *            linked against build/safe/libcplus_safecrt.a and
 *            build/libc/libcplus_hardened.a.  Ordinary C spellings are
 *            remapped by safe_c.h to checked implementations; canaries must
 *            survive every hostile input.
 *
 *   PLAIN:   /usr/bin/clang -std=c99 ... (no -include, no archives).
 *            Raw ISO C / libSystem behaviour.  Each hostile case runs in a
 *            forked child so a crash cannot corrupt the parent.  The parent
 *            records the child's exit status and canary state.
 *
 * Exit status (CHECKED): 0 iff every case behaved as required, else 1.
 * Exit status (PLAIN):   0 always (it is a demonstration/supervisor).
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(CPLUS_SAFE_C_ENABLE)
#include "safe_runtime.h"
#include "safe_error.h"
#include "safe_alloc.h"
#include "safe_io.h"
#  define CHECKED 1
#else
#  define CHECKED 0
#endif

#define CANARY_BYTE 0xA5u
#define CANARY_LEN  16u

typedef struct { unsigned char buf[16]; unsigned char canary[CANARY_LEN]; } gmem;
typedef struct { char buf[8];          unsigned char canary[CANARY_LEN]; } gstr;
typedef struct { char buf[64];         unsigned char canary[CANARY_LEN]; } gname;

static int g_quiet;          /* suppress incidental output in PLAIN children */
static const char *g_outdir = ".";

typedef struct { int acted; int canary_ok; int data_ok; } outcome;

static outcome mk(int acted, int canary_ok, int data_ok) {
    outcome o;
    o.acted = acted;
    o.canary_ok = canary_ok;
    o.data_ok = data_ok;
    return o;
}

static void mem_fill(unsigned char *p, size_t n, unsigned char v) {
    size_t i;
    for (i = 0; i < n; ++i) p[i] = v;
}
static int mem_is(const unsigned char *p, size_t n, unsigned char v) {
    size_t i;
    for (i = 0; i < n; ++i) if (p[i] != v) return 0;
    return 1;
}
static void seq_fill(unsigned char *p, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) p[i] = (unsigned char)('A' + (i % 26u));
}
static int seq_is(const unsigned char *p, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) if (p[i] != (unsigned char)('A' + (i % 26u))) return 0;
    return 1;
}
#define GUARD_INIT(g) mem_fill((g).canary, CANARY_LEN, CANARY_BYTE)
#define GUARD_OK(g)   mem_is((g).canary, CANARY_LEN, CANARY_BYTE)

static char *big_source(size_t n) {
    char *s = (char *)malloc(n + 1u);
    size_t i;
    if (!s) return NULL;
    for (i = 0; i < n; ++i) s[i] = 'Z';
    s[n] = '\0';
    return s;
}

/* fopen is NOT remapped by safe_c.h; use the checked entry point explicitly
 * in the CHECKED build.  Every other ordinary spelling below IS remapped. */
#if CHECKED
#  define SAFE_FOPEN(n, m) cplus_fopen_s((n), (m))
#  define SAFE_PRINTF_S(...) printf_s(__VA_ARGS__)
#  define SAFE_FPRINTF_S(...) fprintf_s(__VA_ARGS__)
#else
#  define SAFE_FOPEN(n, m) fopen((n), (m))
#  define SAFE_PRINTF_S(...) printf(__VA_ARGS__)
#  define SAFE_FPRINTF_S(...) fprintf(__VA_ARGS__)
#endif

/* ---------------- API x CASE dispatch ------------------------------------ */
enum { A_MEMCPY, A_MEMMOVE, A_MEMSET, A_STRCPY, A_STRNCPY, A_STRCAT,
       A_STRNCAT, A_PRINTF, A_FPRINTF, A_FOPEN, A_FREAD, A_FWRITE,
       A_MALLOC, A_CALLOC, A_REALLOC, A_FREE, A_COUNT };
enum { C_VALID, C_NULL, C_SMALL, C_HUGE, C_COUNT };

static const char *const api_names[A_COUNT] = {
    "memcpy", "memmove", "memset", "strcpy", "strncpy", "strcat", "strncat",
    "printf", "fprintf", "fopen", "fread", "fwrite",
    "malloc", "calloc", "realloc", "free"
};
static const char *const case_names[C_COUNT] = { "valid", "null", "small", "huge" };

static outcome run_case(int api, int cas) {
    outcome o = mk(0, 1, 0);
    size_t i;

    switch (api) {
    /* ---- memcpy -------------------------------------------------------- */
    case A_MEMCPY: {
        gmem d; unsigned char src[16];
        GUARD_INIT(d); seq_fill(src, sizeof src);
        if (cas == C_VALID) {
            void *r = memcpy(d.buf, src, 16);
            o = mk(r == d.buf && seq_is(d.buf, 16), GUARD_OK(d), seq_is(d.buf, 16));
        } else if (cas == C_NULL) {
            void *r = memcpy(NULL, src, 16);
            o = mk(r != NULL, GUARD_OK(d), r == NULL);
        } else if (cas == C_SMALL) {
            void *r = memcpy(d.buf, src, 17);
            o = mk(r == d.buf, GUARD_OK(d), r == NULL && GUARD_OK(d));
        } else {
            void *r = memcpy(d.buf, src, (size_t)-1);
            o = mk(r == d.buf, GUARD_OK(d), r == NULL && GUARD_OK(d));
        }
        break;
    }
    /* ---- memmove ------------------------------------------------------- */
    case A_MEMMOVE: {
        gmem d; unsigned char orig[16];
        GUARD_INIT(d); seq_fill(d.buf, sizeof d.buf);
        for (i = 0; i < 16; ++i) orig[i] = d.buf[i];
        if (cas == C_VALID) {
            void *r = memmove(d.buf + 4, d.buf, 8);
            int ok = 1;
            for (i = 0; i < 8; ++i) if (d.buf[4 + i] != orig[i]) ok = 0;
            o = mk(r == d.buf && ok, GUARD_OK(d), ok);
        } else if (cas == C_NULL) {
            void *r = memmove(NULL, d.buf, 8);
            o = mk(r != NULL, GUARD_OK(d), r == NULL);
        } else if (cas == C_SMALL) {
            void *r = memmove(d.buf, orig, 17);
            o = mk(r == d.buf, GUARD_OK(d), r == NULL && GUARD_OK(d));
        } else {
            void *r = memmove(d.buf, orig, (size_t)-1);
            o = mk(r == d.buf, GUARD_OK(d), r == NULL && GUARD_OK(d));
        }
        break;
    }
    /* ---- memset -------------------------------------------------------- */
    case A_MEMSET: {
        gmem d;
        GUARD_INIT(d);
        if (cas == C_VALID) {
            void *r = memset(d.buf, 0x5Au, 16);
            o = mk(r == d.buf && mem_is(d.buf, 16, 0x5Au), GUARD_OK(d),
                   mem_is(d.buf, 16, 0x5Au));
        } else if (cas == C_NULL) {
            void *r = memset(NULL, 0x5Au, 16);
            o = mk(r != NULL, GUARD_OK(d), r == NULL);
        } else if (cas == C_SMALL) {
            void *r = memset(d.buf, 0x5Au, 17);
            o = mk(r == d.buf, GUARD_OK(d), r == NULL && GUARD_OK(d));
        } else {
            void *r = memset(d.buf, 0x5Au, (size_t)-1);
            o = mk(r == d.buf, GUARD_OK(d), r == NULL && GUARD_OK(d));
        }
        break;
    }
    /* ---- strcpy -------------------------------------------------------- */
    case A_STRCPY: {
        gstr d; char *big = NULL;
        GUARD_INIT(d); d.buf[0] = '\0';
        if (cas == C_VALID) {
            char *r = strcpy(d.buf, "ABCDEFG");
            o = mk(r == d.buf && strcmp(d.buf, "ABCDEFG") == 0, GUARD_OK(d),
                   strcmp(d.buf, "ABCDEFG") == 0);
        } else if (cas == C_NULL) {
            char *r = strcpy(NULL, "X");
            o = mk(r != NULL, GUARD_OK(d), r == NULL);
        } else if (cas == C_SMALL) {
            char *r = strcpy(d.buf, "ABCDEFGH"); /* needs 9 bytes, buf is 8 */
            o = mk(r == d.buf, GUARD_OK(d), r == NULL && GUARD_OK(d));
        } else {
            big = big_source(1024u * 1024u);
            if (!big) { o = mk(0, GUARD_OK(d), 0); break; }
            {
                char *r = strcpy(d.buf, big);
                o = mk(r == d.buf, GUARD_OK(d), r == NULL && GUARD_OK(d));
            }
            free(big);
        }
        break;
    }
    /* ---- strncpy ------------------------------------------------------- */
    case A_STRNCPY: {
        gstr d;
        GUARD_INIT(d); d.buf[0] = '\0';
        if (cas == C_VALID) {
            char *r = strncpy(d.buf, "ABCDEFG", 8);
            o = mk(r == d.buf && strcmp(d.buf, "ABCDEFG") == 0, GUARD_OK(d),
                   strcmp(d.buf, "ABCDEFG") == 0);
        } else if (cas == C_NULL) {
            char *r = strncpy(NULL, "X", 1);
            o = mk(r != NULL, GUARD_OK(d), r == NULL);
        } else if (cas == C_SMALL) {
            char *r = strncpy(d.buf, "ABCDEFGH", 8); /* 8 chars + NUL > 8 */
            o = mk(r == d.buf, GUARD_OK(d), r == NULL && GUARD_OK(d));
        } else {
            char *r = strncpy(d.buf, "A", (size_t)-1);
            o = mk(r == d.buf, GUARD_OK(d), r == NULL && GUARD_OK(d));
        }
        break;
    }
    /* ---- strcat -------------------------------------------------------- */
    case A_STRCAT: {
        gstr d; char *big = NULL;
        GUARD_INIT(d); d.buf[0] = '\0';
        if (cas == C_VALID) {
            strcpy(d.buf, "ABC");
            { char *r = strcat(d.buf, "DEFG");
              o = mk(r == d.buf && strcmp(d.buf, "ABCDEFG") == 0, GUARD_OK(d),
                     strcmp(d.buf, "ABCDEFG") == 0); }
        } else if (cas == C_NULL) {
            char *r = strcat(NULL, "X");
            o = mk(r != NULL, GUARD_OK(d), r == NULL);
        } else if (cas == C_SMALL) {
            strcpy(d.buf, "ABCDEFG");
            { char *r = strcat(d.buf, "H"); /* one byte too many */
              o = mk(r == d.buf, GUARD_OK(d), r == NULL && GUARD_OK(d)); }
        } else {
            big = big_source(1024u * 1024u);
            if (!big) { o = mk(0, GUARD_OK(d), 0); break; }
            strcpy(d.buf, "A");
            { char *r = strcat(d.buf, big);
              o = mk(r == d.buf, GUARD_OK(d), r == NULL && GUARD_OK(d)); }
            free(big);
        }
        break;
    }
    /* ---- strncat ------------------------------------------------------- */
    case A_STRNCAT: {
        gstr d;
        GUARD_INIT(d); d.buf[0] = '\0';
        if (cas == C_VALID) {
            strcpy(d.buf, "ABC");
            { char *r = strncat(d.buf, "DEFGH", 4);
              o = mk(r == d.buf && strcmp(d.buf, "ABCDEFG") == 0, GUARD_OK(d),
                     strcmp(d.buf, "ABCDEFG") == 0); }
        } else if (cas == C_NULL) {
            char *r = strncat(NULL, "X", 1);
            o = mk(r != NULL, GUARD_OK(d), r == NULL);
        } else if (cas == C_SMALL) {
            strcpy(d.buf, "ABCDEFG");
            { char *r = strncat(d.buf, "HI", 1); /* one byte too many */
              o = mk(r == d.buf, GUARD_OK(d), r == NULL && GUARD_OK(d)); }
        } else {
            strcpy(d.buf, "A");
            { char *r = strncat(d.buf, "B", (size_t)-1);
              o = mk(r == d.buf, GUARD_OK(d), r == NULL && GUARD_OK(d)); }
        }
        break;
    }
    /* ---- printf -------------------------------------------------------- */
    case A_PRINTF: {
        gmem d; GUARD_INIT(d);
        if (cas == C_VALID) {
            int n = printf("P%dQ\n", 7); /* remapped to cplus_printf in CHECKED */
            o = mk(n == 4, GUARD_OK(d), n == 4);
        } else if (cas == C_NULL) {
            int n = printf(NULL);
            o = mk(n >= 0, GUARD_OK(d), n < 0);
        } else if (cas == C_SMALL) {
            /* hostile %n write into guarded canary.  CHECKED uses printf_s
             * (rejects %n); PLAIN uses raw printf and writes the canary. */
            int n = SAFE_PRINTF_S("%n", (int *)(void *)d.canary);
            o = mk(n >= 0, GUARD_OK(d), n < 0 && GUARD_OK(d));
        } else {
            /* attacker-controlled (non-literal) format: %n into canary. */
            char fmt[3]; fmt[0] = '%'; fmt[1] = 'n'; fmt[2] = '\0';
            int n = SAFE_PRINTF_S(fmt, (int *)(void *)d.canary);
            o = mk(n >= 0, GUARD_OK(d), n < 0 && GUARD_OK(d));
        }
        break;
    }
    /* ---- fprintf ------------------------------------------------------- */
    case A_FPRINTF: {
        gmem d; FILE *f = tmpfile(); GUARD_INIT(d);
        if (!f) { o = mk(0, GUARD_OK(d), 0); break; }
        if (cas == C_VALID) {
            int n = fprintf(f, "F%dG\n", 9);
            int ok = (n == 4);
            rewind(f);
            { char rb[8]; size_t got = fread(rb, 1, sizeof rb, f);
              if (got == 4 && memcmp(rb, "F9G\n", 4) == 0) ok = 1; else ok = 0; }
            o = mk(ok, GUARD_OK(d), ok);
        } else if (cas == C_NULL) {
            int n = fprintf(NULL, "X");
            o = mk(n >= 0, GUARD_OK(d), n < 0);
        } else if (cas == C_SMALL) {
            int n = SAFE_FPRINTF_S(f, "%n", (int *)(void *)d.canary);
            o = mk(n >= 0, GUARD_OK(d), n < 0 && GUARD_OK(d));
        } else {
            char fmt[3]; fmt[0] = '%'; fmt[1] = 'n'; fmt[2] = '\0';
            int n = SAFE_FPRINTF_S(f, fmt, (int *)(void *)d.canary);
            o = mk(n >= 0, GUARD_OK(d), n < 0 && GUARD_OK(d));
        }
        fclose(f);
        break;
    }
    /* ---- fopen --------------------------------------------------------- */
    case A_FOPEN: {
        gname gn; GUARD_INIT(gn);
        if (cas == C_VALID) {
            char path[160]; FILE *f; int ok = 0;
            snprintf(path, sizeof path, "%s/stdc_safety_fopen.txt", g_outdir);
            f = SAFE_FOPEN(path, "w+");
            if (f) {
                if (fputs("OK", f) >= 0) ok = 1;
                fclose(f);
                f = SAFE_FOPEN(path, "r");
                if (f) { char rb[4]; size_t got = fread(rb, 1, sizeof rb, f);
                         if (got == 2 && rb[0] == 'O' && rb[1] == 'K') ok = 1; else ok = 0;
                         fclose(f); }
            }
            o = mk(ok, GUARD_OK(gn), ok);
        } else if (cas == C_NULL) {
            FILE *f = SAFE_FOPEN(NULL, "r");
            o = mk(f != NULL, GUARD_OK(gn), f == NULL);
        } else if (cas == C_SMALL) {
            /* over-long name copied into a guarded 64-byte buffer first */
            char toolong[80];
            mem_fill((unsigned char *)toolong, sizeof toolong, (unsigned char)'X');
            toolong[sizeof toolong - 1u] = '\0';
            mem_fill((unsigned char *)gn.buf, sizeof gn.buf, 0);
            {
                char *r = strcpy(gn.buf, toolong); /* CHECKED refuses */
                FILE *f = SAFE_FOPEN(gn.buf, "r");
                o = mk(r != NULL, GUARD_OK(gn), r == NULL && f == NULL);
            }
        } else {
            char *name = big_source(1024u * 1024u);
            if (!name) { o = mk(0, GUARD_OK(gn), 0); break; }
            { FILE *f = SAFE_FOPEN(name, "r");
              o = mk(f != NULL, GUARD_OK(gn), f == NULL); }
            free(name);
        }
        break;
    }
    /* ---- fread --------------------------------------------------------- */
    case A_FREAD: {
        gmem d; FILE *f = tmpfile(); GUARD_INIT(d);
        unsigned char src[32]; seq_fill(src, sizeof src);
        if (!f) { o = mk(0, GUARD_OK(d), 0); break; }
        fwrite(src, 1, sizeof src, f); rewind(f);
        if (cas == C_VALID) {
            size_t n = fread(d.buf, 1, 16, f);
            o = mk(n == 16 && seq_is(d.buf, 16), GUARD_OK(d),
                   n == 16 && seq_is(d.buf, 16));
        } else if (cas == C_NULL) {
            size_t n = fread(NULL, 1, 1, f);
            o = mk(n != 0, GUARD_OK(d), n == 0);
        } else if (cas == C_SMALL) {
            size_t want = 17;
#if CHECKED
            /* Annex-K bounded read clamps to bmax=16 -> no canary write */
            size_t n = cplus_fread_s(d.buf, 16, 1, want, f);
#else
            size_t n = fread(d.buf, 1, want, f);
#endif
            o = mk(n == want, GUARD_OK(d), n < want && GUARD_OK(d));
        } else {
            size_t n = fread(d.buf, (size_t)-1, 2, f); /* product overflows */
            o = mk(n != 0, GUARD_OK(d), n == 0 && GUARD_OK(d));
        }
        fclose(f);
        break;
    }
    /* ---- fwrite -------------------------------------------------------- */
    case A_FWRITE: {
        gmem d; FILE *f = tmpfile(); GUARD_INIT(d); seq_fill(d.buf, 16);
        if (!f) { o = mk(0, GUARD_OK(d), 0); break; }
        if (cas == C_VALID) {
            size_t n = fwrite(d.buf, 1, 16, f);
            unsigned char rb[16]; int ok = (n == 16);
            rewind(f);
            if (fread(rb, 1, 16, f) == 16 && seq_is(rb, 16)) ok = 1; else ok = 0;
            o = mk(ok, GUARD_OK(d), ok);
        } else if (cas == C_NULL) {
            size_t n = fwrite(NULL, 1, 1, f);
            o = mk(n != 0, GUARD_OK(d), n == 0);
        } else if (cas == C_SMALL) {
            /* destination file has a 16-byte canary after a 16-byte region */
            unsigned char fb[32]; unsigned char rb[32];
            mem_fill(fb, 16, (unsigned char)'B');
            mem_fill(fb + 16, 16, CANARY_BYTE);
            fwrite(fb, 1, sizeof fb, f); rewind(f);
            {
#if CHECKED
                size_t n = cplus_fwrite_s(d.buf, 16, 1, 17, f); /* clamps to 16 */
#else
                size_t n = fwrite(d.buf, 1, 17, f); /* writes 1 byte into file canary */
#endif
                rewind(f);
                if (fread(rb, 1, sizeof rb, f) != sizeof rb) rb[0] = 0;
                o = mk(n == 17, mem_is(rb + 16, 16, CANARY_BYTE),
                       n < 17 && mem_is(rb + 16, 16, CANARY_BYTE));
            }
        } else {
            size_t n = fwrite(d.buf, (size_t)-1, 2, f); /* product overflows */
            o = mk(n != 0, GUARD_OK(d), n == 0 && GUARD_OK(d));
        }
        fclose(f);
        break;
    }
    /* ---- malloc -------------------------------------------------------- */
    case A_MALLOC: {
        gmem guard; GUARD_INIT(guard);
        if (cas == C_VALID) {
            unsigned char *p = (unsigned char *)malloc(16);
            void *r = p ? memset(p, 0x3Cu, 16) : NULL;
            int ok = (p != NULL && r == p && mem_is(p, 16, 0x3Cu));
            free(p);
            o = mk(ok, GUARD_OK(guard), ok);
        } else if (cas == C_NULL) {
            void *p = malloc((size_t)-1);
            o = mk(p != NULL, GUARD_OK(guard), p == NULL);
        } else if (cas == C_SMALL) {
            unsigned char *p = (unsigned char *)malloc(8);
            void *r = NULL;
            if (!p) { o = mk(0, GUARD_OK(guard), 0); break; }
            r = memset(p + 1, 0x41, 8); /* one byte beyond tracked 8 */
            o = mk(r != NULL, GUARD_OK(guard), r == NULL && GUARD_OK(guard));
            free(p);
        } else {
            unsigned char *p = (unsigned char *)malloc(8);
            void *r = NULL;
            if (!p) { o = mk(0, GUARD_OK(guard), 0); break; }
            r = memset(p, 0x41, (size_t)-1);
            o = mk(r != NULL, GUARD_OK(guard), r == NULL && GUARD_OK(guard));
            free(p);
        }
        break;
    }
    /* ---- calloc -------------------------------------------------------- */
    case A_CALLOC: {
        gmem guard; GUARD_INIT(guard);
        if (cas == C_VALID) {
            unsigned char *p = (unsigned char *)calloc(4, 4);
            int ok = (p != NULL && mem_is(p, 16, 0));
            free(p);
            o = mk(ok, GUARD_OK(guard), ok);
        } else if (cas == C_NULL) {
            void *p = calloc((size_t)-1, 2); /* overflow */
            o = mk(p != NULL, GUARD_OK(guard), p == NULL);
        } else if (cas == C_SMALL) {
            unsigned char *p = (unsigned char *)calloc(1, 8);
            void *r = NULL;
            if (!p) { o = mk(0, GUARD_OK(guard), 0); break; }
            r = memset(p + 1, 0x41, 8);
            o = mk(r != NULL, GUARD_OK(guard), r == NULL && GUARD_OK(guard));
            free(p);
        } else {
            unsigned char *p = (unsigned char *)calloc(1, 8);
            void *r = NULL;
            if (!p) { o = mk(0, GUARD_OK(guard), 0); break; }
            r = memset(p, 0x41, (size_t)-1);
            o = mk(r != NULL, GUARD_OK(guard), r == NULL && GUARD_OK(guard));
            free(p);
        }
        break;
    }
    /* ---- realloc ------------------------------------------------------- */
    case A_REALLOC: {
        gmem guard; GUARD_INIT(guard);
        if (cas == C_VALID) {
            unsigned char *p = (unsigned char *)malloc(8);
            unsigned char *q = NULL;
            if (!p) { o = mk(0, GUARD_OK(guard), 0); break; }
            memset(p, 0x11, 8);
            q = (unsigned char *)realloc(p, 16);
            { int ok = (q != NULL && mem_is(q, 8, 0x11));
              free(q); o = mk(ok, GUARD_OK(guard), ok); }
        } else if (cas == C_NULL) {
            unsigned char *p = (unsigned char *)malloc(8);
            void *q = NULL;
            if (!p) { o = mk(0, GUARD_OK(guard), 0); break; }
            q = realloc(p, (size_t)-1);
            o = mk(q != NULL, GUARD_OK(guard), q == NULL);
            free(p); /* original block must still be live/untouched */
        } else if (cas == C_SMALL) {
            unsigned char *p = (unsigned char *)malloc(8);
            void *r = NULL;
            if (!p) { o = mk(0, GUARD_OK(guard), 0); break; }
            r = memset(p + 1, 0x41, 8);
            o = mk(r != NULL, GUARD_OK(guard), r == NULL && GUARD_OK(guard));
            free(p);
        } else {
            unsigned char *p = (unsigned char *)malloc(8);
            void *r = NULL;
            if (!p) { o = mk(0, GUARD_OK(guard), 0); break; }
            r = memset(p, 0x41, (size_t)-1);
            o = mk(r != NULL, GUARD_OK(guard), r == NULL && GUARD_OK(guard));
            free(p);
        }
        break;
    }
    /* ---- free ---------------------------------------------------------- */
    case A_FREE: {
        gmem guard; GUARD_INIT(guard);
        if (cas == C_VALID) {
            unsigned char *p = (unsigned char *)malloc(16);
            int ok = 0;
            if (!p) { o = mk(0, GUARD_OK(guard), 0); break; }
            memset(p, 0x77, 16);
            free(p); /* remapped to free_s */
#if CHECKED
            ok = !cplus_safe_ptr_is_live(p);
#else
            ok = 1;
#endif
            o = mk(ok, GUARD_OK(guard), ok);
        } else if (cas == C_NULL) {
            /* invalid / NULL-like pointer (address 1) */
            void *bad = (void *)(uintptr_t)1;
            free(bad);
#if CHECKED
            o = mk(free_checked_s(bad) != CPLUS_SAFE_EINVAL, GUARD_OK(guard),
                   free_checked_s(bad) == CPLUS_SAFE_EINVAL);
#else
            o = mk(1, GUARD_OK(guard), 0); /* raw free(bad) crashes in child */
#endif
        } else if (cas == C_SMALL) {
            /* double free: the "one free too many" lifetime slip */
            unsigned char *p = (unsigned char *)malloc(16);
            if (!p) { o = mk(0, GUARD_OK(guard), 0); break; }
            free(p);
            free(p); /* CHECKED: refused; PLAIN: heap corruption/abort */
#if CHECKED
            o = mk(free_checked_s(p) != CPLUS_SAFE_EINVAL, GUARD_OK(guard),
                   free_checked_s(p) == CPLUS_SAFE_EINVAL);
#else
            o = mk(1, GUARD_OK(guard), 0);
#endif
        } else {
            void *bad = (void *)(uintptr_t)(size_t)-1; /* huge invalid pointer */
            free(bad);
#if CHECKED
            o = mk(free_checked_s(bad) != CPLUS_SAFE_EINVAL, GUARD_OK(guard),
                   free_checked_s(bad) == CPLUS_SAFE_EINVAL);
#else
            o = mk(1, GUARD_OK(guard), 0);
#endif
        }
        break;
    }
    default:
        o = mk(0, 0, 0);
        break;
    }
    return o;
}

/* ---------------- counters + reporting ---------------------------------- */
static unsigned long g_cases, g_refused, g_canary_ok, g_failures;

static void emit_checked(int api, int cas, const outcome *o) {
    int hostile = (cas != C_VALID);
    int pass = hostile ? (!o->acted && o->data_ok && o->canary_ok)
                       : (o->acted && o->data_ok && o->canary_ok);
    printf("stdc_safety %s %s %s %d\n", api_names[api], case_names[cas],
           hostile ? "REFUSED" : "OK", o->canary_ok ? 1 : 0);
    ++g_cases;
    if (hostile) ++g_refused;
    if (o->canary_ok) ++g_canary_ok;
    if (!pass) ++g_failures;
}

int main(int argc, char **argv) {
    int api, cas;
    if (argc > 1) g_outdir = argv[1];

#if CHECKED
    /* Keep unguarded SafeCRT violations diagnosable instead of _exit(70) so
     * the harness can print every case and produce a final summary. */
    cplus_runtime_set_violation_trap(0);
    cplus_safe_set_unknown_capacity_mode(1);

    for (api = 0; api < A_COUNT; ++api) {
        for (cas = 0; cas < C_COUNT; ++cas) {
            outcome o = run_case(api, cas);
            emit_checked(api, cas, &o);
        }
    }
    printf("STDC_SAFETY %lu %lu %lu %lu\n",
           g_cases, g_refused, g_canary_ok, g_failures);
    fflush(stdout);
    return g_failures ? 1 : 0;
#else
    (void)g_quiet;
    for (api = 0; api < A_COUNT; ++api) {
        for (cas = 0; cas < C_COUNT; ++cas) {
            pid_t pid = fork();
            if (pid == 0) {
                outcome o = run_case(api, cas);
                int hostile = (cas != C_VALID);
                int pass = hostile ? (!o.acted && o.data_ok && o.canary_ok)
                                   : (o.acted && o.data_ok && o.canary_ok);
                printf("plain %s %s %s canary=%d acted=%d data=%d %s\n",
                       api_names[api], case_names[cas],
                       hostile ? "HOSTILE" : "VALID",
                       o.canary_ok, o.acted, o.data_ok,
                       pass ? "PASS" : "DIFF");
                fflush(stdout);
                _exit(0);
            } else if (pid > 0) {
                int status = 0;
                waitpid(pid, &status, 0);
                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                    int sig = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
                    printf("plain %s %s CRASH signal=%d exit=%d\n",
                           api_names[api], case_names[cas], sig,
                           WIFEXITED(status) ? WEXITSTATUS(status) : -1);
                    ++g_failures;
                }
            } else {
                ++g_failures;
            }
        }
    }
    printf("PLAIN_SUMMARY crashes_or_abnormal=%lu\n", g_failures);
    fflush(stdout);
    return 0;
#endif
}
