/* Extra libc stdio coverage, including regressions for the plain fclose
 * double-close cache (AUDIT #2 and #3): a 256-entry ring used to wrap and
 * forget markers, and a recycled FILE* address left stale in the cache made
 * cplus_fclose reject a valid first close (fd leak). */
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int cplus_putchar(int); int cplus_getchar(void); int cplus_puts(const char*); void cplus_perror(const char*);
FILE *cplus_fopen(const char*,const char*); int cplus_fclose(FILE*);
/* Private registration hook: every creation path must unmark a recycled
 * address.  Declared here so a raw-fopen'd stream can simulate registration. */
void cplus_stdio_register(FILE *);

static int count_fds(void) {
    DIR *d = opendir("/dev/fd");
    struct dirent *e;
    int n = 0;
    if (!d) return -1;
    while ((e = readdir(d)) != NULL) { (void)e; ++n; }
    closedir(d);
    return n;
}

/* Run this FIRST, with a pristine wrapper cache: a raw host stream whose
 * address was recycled from another host-closed stream is not in any closed
 * marker table, so the wrapper must accept its first close and leak no fd. */
static void test_raw_recycled_unknown(void) {
    int before = count_fds(), after;
    FILE *raw1 = fopen("/dev/null", "r");
    FILE *raw2 = NULL;
    int i;
    assert(raw1 != NULL);
    assert(fclose(raw1) == 0);
    for (i = 0; i < 64 && !raw2; ++i) {
        FILE *f = fopen("/dev/null", "r");
        assert(f != NULL);
        if (f == raw1) raw2 = f;
        else assert(fclose(f) == 0);
    }
    assert(raw2 != NULL);
    assert(cplus_fclose(raw2) == 0);
    after = count_fds();
    if (before >= 0 && after >= 0) assert(after <= before + 2);
}

/* A raw stream at an address recycled from a *wrapper*-closed stream carries
 * a stale closed marker.  Registering the new object clears that marker and
 * makes its first close succeed (the documented no-false-rejection path). */
static void test_raw_recycled_registered(void) {
    int before = count_fds(), after;
    FILE *a = cplus_fopen("/dev/null", "r");
    FILE *raw = NULL;
    int i;
    assert(a != NULL);
    assert(cplus_fclose(a) == 0); /* a's address is now marked closed */
    for (i = 0; i < 2000 && !raw; ++i) {
        FILE *f = fopen("/dev/null", "r");
        assert(f != NULL);
        if (f == a) raw = f;
        else assert(fclose(f) == 0);
    }
    assert(raw == a);
    cplus_stdio_register(raw); /* unmark stale closed entry, track as live */
    assert(cplus_fclose(raw) == 0);
    after = count_fds();
    if (before >= 0 && after >= 0) assert(after <= before + 2);

    /* Unregistered raw streams at a wrapper-closed address cannot be
     * distinguished from a genuine double close, so the guard fails closed.
     * Reclaim the descriptor explicitly; there is no net fd leak. */
    a = cplus_fopen("/dev/null", "r");
    assert(a != NULL);
    assert(cplus_fclose(a) == 0);
    raw = NULL;
    for (i = 0; i < 2000 && !raw; ++i) {
        FILE *f = fopen("/dev/null", "r");
        assert(f != NULL);
        if (f == a) raw = f;
        else assert(fclose(f) == 0);
    }
    assert(raw == a);
    errno = 0;
    assert(cplus_fclose(raw) == EOF && errno == EBADF);
    assert(fclose(raw) == 0);
    after = count_fds();
    if (before >= 0 && after >= 0) assert(after <= before + 2);
}

/* 300 sequential closes (past the old 256-entry ring wrap point): the first
 * stream's double close must still be refused, and a second wave must reuse
 * addresses without leaking descriptors or losing protection. */
static void test_300_close_stress(void) {
    enum { N = 300 };
    FILE *arr[N];
    int before = count_fds(), after;
    int i;
    for (i = 0; i < N; ++i) {
        arr[i] = cplus_fopen("/dev/null", "r");
        assert(arr[i] != NULL);
    }
    for (i = 0; i < N; ++i) assert(cplus_fclose(arr[i]) == 0);

    errno = 0;
    assert(cplus_fclose(arr[0]) == EOF && errno == EBADF);

    for (i = 0; i < N; ++i) {
        arr[i] = cplus_fopen("/dev/null", "r");
        assert(arr[i] != NULL);
    }
    for (i = 0; i < N; ++i) assert(cplus_fclose(arr[i]) == 0);

    errno = 0;
    assert(cplus_fclose(arr[N - 1]) == EOF && errno == EBADF);
    after = count_fds();
    if (before >= 0 && after >= 0) assert(after <= before + 2);
}

int main(void){
    for(int i=0;i<10;i++) assert(cplus_puts("ok")>=0);
    for(int i=0;i<10;i++) assert(cplus_putchar('\n')=='\n');
    /* getchar from stdin redirected */
    FILE*w=fopen("/tmp/cplus_getchar.tmp","wb"); fputs("A",w); fclose(w);
    for(int i=0;i<10;i++){
        FILE*old=stdin; stdin=fopen("/tmp/cplus_getchar.tmp","rb");
        int c=cplus_getchar(); if(stdin) fclose(stdin); stdin=old; assert(c=='A'||c==EOF);
    }
    for(int i=0;i<10;i++) cplus_perror("cplus");
    remove("/tmp/cplus_getchar.tmp");

    test_raw_recycled_unknown();
    test_raw_recycled_registered();
    test_300_close_stress();
    return 0;
}
