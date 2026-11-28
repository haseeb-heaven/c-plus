/*
 * One tiny C fixture, parameterized by -DF_<ordinary-name>.
 *
 * Each case exercises an ORDINARY ISO C spelling (never an _s spelling) in
 * two modes selected by argv[1]:
 *   valid   -> print one deterministic line to stdout and exit 0
 *   hostile -> print "REFUSED" to stdout and exit 70 if the checked build
 *              rejects the input; otherwise print "NO-PROTECTION" and exit 0
 *
 * The same source is compiled twice by verify.sh:
 *   plain   : no force-include, host CRT symbols, no extra archives
 *   checked : -include stdlib/safe/safe_c.h + both shipped archives
 * Only the preprocessing of the including TU differs; no symbol is
 * interposed. stdout is unbuffered so an _exit(70) refusal still flushes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

static void refuse(void) {
    fputs("REFUSED\n", stdout);
    fflush(stdout);
    exit(70);
}

static int is_hostile(int argc, char **argv) {
    return argc > 1 && strcmp(argv[1], "hostile") == 0;
}

int main(int argc, char **argv) {
    int hostile = is_hostile(argc, argv);
    setvbuf(stdout, NULL, _IONBF, 0);

#if defined(F_MALLOC)
    if (hostile) {
        void *p = malloc((size_t)-1);
        if (!p) refuse();
        fputs("NO-PROTECTION\n", stdout);
        free(p);
        return 0;
    }
    {
        unsigned char *p = (unsigned char *)malloc(64);
        if (!p) return 1;
        p[0] = 1; p[63] = 2;
        printf("malloc:ok %d %d\n", p[0], p[63]);
        free(p);
    }

#elif defined(F_CALLOC)
    if (hostile) {
        void *p = calloc((size_t)-1, 2);
        if (!p) refuse();
        fputs("NO-PROTECTION\n", stdout);
        free(p);
        return 0;
    }
    {
        int *p = (int *)calloc(4, sizeof(int));
        if (!p) return 1;
        p[3] = 7;
        printf("calloc:ok %d %d\n", p[0], p[3]);
        free(p);
    }

#elif defined(F_REALLOC)
    if (hostile) {
        int stack_obj = 0;
        void *r = realloc(&stack_obj, 64); /* unowned pointer */
        if (!r) refuse();
        fputs("NO-PROTECTION\n", stdout);
        return 0;
    }
    {
        int *p = (int *)malloc(8);
        if (!p) return 1;
        p[0] = 7;
        p = (int *)realloc(p, 64);
        if (!p) return 1;
        printf("realloc:ok %d\n", p[0]);
        free(p);
    }

#elif defined(F_FREE)
    if (hostile) {
        void *p = malloc(8);
        if (!p) return 1;
        free(p);
        errno = 0;
        free(p); /* double free */
        if (errno) refuse();
        fputs("NO-PROTECTION\n", stdout);
        return 0;
    }
    {
        void *p = malloc(8);
        if (!p) return 1;
        free(p);
        fputs("free:ok\n", stdout);
    }

#elif defined(F_MEMSET)
    if (hostile) {
        char b[8];
        volatile char sink;
        memset(b, 0x41, 9); /* one-over, BOS known */
        sink = b[0]; (void)sink;
        fputs("NO-PROTECTION\n", stdout);
        return 0;
    }
    {
        char b[8];
        memset(b, 0x41, 8);
        printf("memset:ok %d %d\n", (unsigned char)b[0], (unsigned char)b[7]);
    }

#elif defined(F_MEMCPY)
    if (hostile) {
        char s[9] = "01234567";
        char d[8];
        volatile char sink;
        memcpy(d, s, 9); /* one-over, BOS known */
        sink = d[0]; (void)sink;
        fputs("NO-PROTECTION\n", stdout);
        return 0;
    }
    {
        char s[8] = "01234567";
        char d[9] = {0};
        memcpy(d, s, 8);
        printf("memcpy:ok %s\n", d);
    }

#elif defined(F_MEMMOVE)
    if (hostile) {
        char s[9] = "01234567";
        char d[8];
        volatile char sink;
        memmove(d, s, 9); /* one-over, BOS known */
        sink = d[0]; (void)sink;
        fputs("NO-PROTECTION\n", stdout);
        return 0;
    }
    {
        char b[16] = "0123456789";
        memmove(b + 4, b, 8); /* valid overlap */
        printf("memmove:ok %s\n", b);
    }

#elif defined(F_MEMCMP)
    if (hostile) {
        char a[8] = {0};
        char b[8] = {0};
        errno = 0;
        (void)memcmp(a, b, 9); /* one-over on both operands */
        if (errno) refuse();
        fputs("NO-PROTECTION\n", stdout);
        return 0;
    }
    {
        char a[8] = "abc";
        char b[8] = "abd";
        printf("memcmp:ok %d\n", memcmp(a, b, 8) < 0 ? -1 : 1);
    }

#elif defined(F_STRCPY)
    if (hostile) {
        char d[8];
        volatile char sink;
        errno = 0;
        strcpy(d, "0123456789"); /* one-over: NULL return + errno ERANGE */
        sink = d[0]; (void)sink;
        if (errno != 0) refuse();
        fputs("NO-PROTECTION\n", stdout);
        return 0;
    }
    {
        char d[16];
        strcpy(d, "hello");
        printf("strcpy:ok %s\n", d);
    }

#elif defined(F_STRNCPY)
    if (hostile) {
        char d[8];
        volatile char sink;
        errno = 0;
        strncpy(d, "0123456789", 10); /* one-over: NULL return + errno ERANGE */
        sink = d[0]; (void)sink;
        if (errno != 0) refuse();
        fputs("NO-PROTECTION\n", stdout);
        return 0;
    }
    {
        char d[16] = {0};
        strncpy(d, "hello", 5);
        printf("strncpy:ok %s\n", d);
    }

#elif defined(F_STRCAT)
    if (hostile) {
        char d[4] = "ab";
        volatile char sink;
        errno = 0;
        strcat(d, "0123456789"); /* one-over: NULL return + errno ERANGE */
        sink = d[0]; (void)sink;
        if (errno != 0) refuse();
        fputs("NO-PROTECTION\n", stdout);
        return 0;
    }
    {
        char d[16] = "ab";
        strcat(d, "cd");
        printf("strcat:ok %s\n", d);
    }

#elif defined(F_STRNCAT)
    if (hostile) {
        char d[4] = "ab";
        volatile char sink;
        errno = 0;
        strncat(d, "0123456789", 9); /* one-over: NULL return + errno ERANGE */
        sink = d[0]; (void)sink;
        if (errno != 0) refuse();
        fputs("NO-PROTECTION\n", stdout);
        return 0;
    }
    {
        char d[16] = "ab";
        strncat(d, "cdef", 4);
        printf("strncat:ok %s\n", d);
    }

#elif defined(F_FREAD)
    if (hostile) {
        unsigned char b[4];
        FILE *bad = (FILE *)0;
        errno = 0;
        if (fread(b, 1, 1, bad) == 0 && errno) refuse();
        fputs("NO-PROTECTION\n", stdout);
        return 0;
    }
    {
        FILE *f = tmpfile();
        unsigned char b[4] = {0};
        size_t n;
        if (!f) return 1;
        fputs("abc", f);
        rewind(f);
        n = fread(b, 1, 3, f);
        printf("fread:ok %zu %s\n", n, b);
        fclose(f);
    }

#elif defined(F_FWRITE)
    if (hostile) {
        FILE *bad = (FILE *)0;
        errno = 0;
        if (fwrite("abc", 1, 3, bad) == 0 && errno) refuse();
        fputs("NO-PROTECTION\n", stdout);
        return 0;
    }
    {
        FILE *f = tmpfile();
        size_t n;
        if (!f) return 1;
        n = fwrite("abc", 1, 3, f);
        printf("fwrite:ok %zu\n", n);
        fclose(f);
    }

#elif defined(F_PRINTF)
    if (hostile) {
        const char *volatile bad = (const char *)0;
        errno = 0;
        if (printf(bad) < 0 && errno) refuse();
        fputs("NO-PROTECTION\n", stdout);
        return 0;
    }
    printf("printf:ok %d %s\n", 42, "x");

#elif defined(F_FPRINTF)
    if (hostile) {
        const char *volatile bad = (const char *)0;
        errno = 0;
        if (fprintf(stdout, bad) < 0 && errno) refuse();
        fputs("NO-PROTECTION\n", stdout);
        return 0;
    }
    fprintf(stdout, "fprintf:ok %d\n", 7);

#else
#error "define exactly one F_<name> to select a case"
#endif

    return 0;
}
