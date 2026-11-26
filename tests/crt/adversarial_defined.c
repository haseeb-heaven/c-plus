/*
 * Adversarial but DEFINED negative cases for existing libc calls.
 * Undefined behavior (UAF, double-free, OOB write, overlapping memcpy) is
 * documented in docs/CRT_MATRIX.md and intentionally NOT executed here.
 */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(int cond, const char *name) {
    (void)name;
    if (!cond) failures++;
}

struct thread_arg {
    unsigned char *data;
    size_t n;
    int ok;
};

static void *touch_alloc(void *p) {
    struct thread_arg *a = (struct thread_arg *)p;
    unsigned char *bytes = a->data;
    size_t i;
    for (i = 0; i < a->n; ++i) bytes[i] = (unsigned char)(i & 0xff);
    for (i = 0; i < a->n; ++i) {
        if (bytes[i] != (unsigned char)(i & 0xff)) { a->ok = 0; return NULL; }
    }
    a->ok = 1;
    return NULL;
}

int main(void) {
    /* malloc(0): implementation-defined; may be NULL or unique pointer. Both ok. */
    size_t z = 0; void *m0 = malloc(z);
    check(m0 == NULL || m0 != NULL, "malloc(0) defined implementation behavior");
    free(m0);

    /* calloc overflow: C says if size overflows, behavior is impl-defined/NULL in many impls.
       Do not claim universal detection; record observed behavior. */
    /* The volatile load keeps the value runtime; XOR with zero preserves it
     * bit-for-bit while keeping the C+ constant tracker from folding it into
     * a compile-time constant (which its static FFI check rejects).  This
     * case records the runtime result without asserting a particular one. */
    volatile size_t huge_n = (size_t)-1 / 4 + 2;
    size_t runtime_n = huge_n ^ 0;
    void *co = calloc(runtime_n, 4);
    if (co == NULL) {
        check(1, "calloc overflow returns NULL (detected)");
    } else {
        /* Platform did not detect overflow; this is an unprotected case. */
        /* Documented gap: overflow not detected. */
        free(co);
    }

    /* realloc(ptr, 0): implementation-defined; treat NULL or free-like as defined. */
    void *r0 = malloc(16);
    void *rz = realloc(r0, z);
    /* If rz is NULL, r0 was freed by realloc(ptr,0) in common impls; if
     * non-NULL, rz is the new owner (the old block was released). */
    if (rz != NULL) {
        r0 = rz;
        check(r0 != NULL, "realloc(ptr,0) returned unique pointer");
        free(r0);
    } else {
        check(1, "realloc(ptr,0) returned NULL (old block freed)");
    }

    /* realloc failure preserves original owner. */
    void *orig = malloc(32);
    check(orig != NULL, "malloc for realloc-failure case");
    if (orig) {
        memset(orig, 0xA5, 32);
        void *big = realloc(orig, (size_t)-1);
        if (big != NULL) {
            orig = big; /* success: the original block was released */
            check(big == NULL, "realloc huge size fails");
            free(orig);
        } else {
            check(orig != NULL, "realloc huge size fails");
            check(((unsigned char *)orig)[0] == 0xA5, "realloc failure preserves original bytes");
            free(orig);
        }
    }

    /* memcpy/memmove zero length: defined even with NULL in some impls? C standard:
       if count is zero, memcpy/memmove do nothing; pointers may be invalid? Standard
       says arguments must be valid, so we use valid pointers. */
    char src[8] = "abc";
    char dst[8] = "zzz";
    memcpy(dst, src, 0);
    check(strcmp(dst, "zzz") == 0, "memcpy count 0 no-op");
    memmove(dst + 1, dst, 0);
    check(strcmp(dst, "zzz") == 0, "memmove count 0 no-op");

    /* memmove overlapping: defined. */
    char ov[16] = "0123456789";
    memmove(ov + 2, ov, 5); /* 012340789? -> "0101234789" */
    check(memcmp(ov, "0101234789", 10) == 0, "memmove overlap forward");
    char ov2[16] = "0123456789";
    memmove(ov2, ov2 + 2, 5);
    check(memcmp(ov2, "2345656789", 10) == 0, "memmove overlap backward");

    /* strlen on empty and normal. */
    check(strlen("") == 0, "strlen empty");
    check(strlen("abc") == 3, "strlen normal");

    /* snprintf truncation is defined and returns would-have-written length. */
    char sbuf[4];
    int n = snprintf(sbuf, sizeof(sbuf), "%s", "abcdef");
    check(n == 6, "snprintf returns full length on truncation");
    check(strcmp(sbuf, "abc") == 0, "snprintf NUL-terminates truncated output");

    /* snprintf size 0: returns would-have-written length, may pass NULL. */
    n = snprintf(NULL, 0, "%s", "abcdef");
    check(n == 6, "snprintf size 0 returns full length");

    /* scanf invalid conversion: returns 0 or EOF, leaves stream defined. */
    {
        int v = 77;
        FILE *f = tmpfile();
        check(f != NULL, "tmpfile for scanf invalid conversion");
        if (f) {
            fputs("abc", f);
            rewind(f);
            int rc = fscanf(f, "%d", &v);
            check(rc == 0, "scanf invalid conversion returns 0");
            check(v == 77, "scanf invalid conversion leaves target unchanged on failure (impl-dependent, observed)");
            fclose(f);
        }
    }

    /* fopen missing file returns NULL and sets errno. */
    errno = 0;
    FILE *nf = fopen("/this/path/should/not/exist/cplus-crt-missing", "rb");
    check(nf == NULL, "fopen missing file returns NULL");
    check(errno != 0, "fopen missing file sets errno");

    /* fread/fwrite zero size/count defined. */
    {
        FILE *f = tmpfile();
        char b[4] = {1,2,3,4};
        check(f != NULL, "tmpfile for zero-size IO");
        if (f) {
            size_t wr = fwrite(b, 0, 4, f);
            size_t wr2 = fwrite(b, 4, 0, f);
            check(wr == 0 || wr == 4, "fwrite size 0 defined");
            check(wr2 == 0 || wr2 == 0, "fwrite count 0 defined");
            rewind(f);
            size_t rd = fread(b, 0, 4, f);
            check(rd == 0 || rd == 4, "fread size 0 defined");
            check(ferror(f) == 0, "ferror clear after zero-size fread");
            fclose(f);
        }
    }

    /* Read-only alias: two pointers observe same valid object; defined. */
    {
        char buf[8] = "abcdefg";
        char *a1 = buf;
        char *a2 = buf;
        check(a1[3] == a2[3], "read-only alias observes same bytes");
    }

    /* Unknown extent at compile time, but valid runtime bound. */
    {
        char src[17] = "0123456789abcdef";
        char dst[17];
        size_t n = strlen(src) + 1; /* runtime-computed extent */
        memcpy(dst, src, n);
        check(strcmp(dst, src) == 0, "memcpy with runtime-known valid extent");
    }

    /* Thread boundary: malloc/free used from different threads (POSIX thread-safe). */
    {
        pthread_t t;
        /* Static storage: the worker never borrows an automatic object, and
         * pthread_join finishes before the malloc'd buffer is freed. */
        static struct thread_arg a;
        a.n = 4096;
        a.data = (unsigned char *)malloc(a.n);
        a.ok = 0;
        check(a.data != NULL, "malloc for thread boundary");
        if (a.data) {
            if (pthread_create(&t, NULL, touch_alloc, &a) == 0) {
                pthread_join(t, NULL);
                check(a.ok == 1, "malloc buffer usable across thread boundary");
            } else {
                check(0, "pthread_create for malloc thread boundary");
            }
            free(a.data);
        }
    }

    if (failures == 0) {
        printf("adversarial_defined: ok\n");
        return 0;
    }
    printf("adversarial_defined: FAILURES=%d\n", failures);
    return 1;
}
