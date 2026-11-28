/* Focused probe: does a self-detected constraint violation with a large
 * (but <= RSIZE_MAX_MEM) caller-supplied dmax cause a wild memset? */
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>

typedef int errno_t;
typedef size_t rsize_t;

extern errno_t cplus_sc_memcpy_s(void *, rsize_t, const void *, rsize_t);
extern errno_t cplus_sc_memmove_s(void *, rsize_t, const void *, rsize_t);
extern errno_t cplus_sc_memset_s(void *, rsize_t, int, rsize_t);
extern errno_t cplus_sc_memcmp_s(const void *, rsize_t, const void *, rsize_t, int *);
extern errno_t cplus_sc_memzero_s(void *, rsize_t);
extern errno_t cplus_sc_strcpy_s(char *, rsize_t, const char *);
extern errno_t cplus_sc_strncpy_s(char *, rsize_t, const char *, rsize_t);
extern errno_t cplus_sc_strcat_s(char *, rsize_t, const char *);
extern errno_t cplus_sc_strncat_s(char *, rsize_t, const char *, rsize_t);
extern rsize_t  cplus_sc_strnlen_s(const char *, rsize_t);
extern errno_t cplus_sc_strcmp_s(const char *, rsize_t, const char *, int *);
extern errno_t cplus_sc_strerror_s(char *, rsize_t, int);
extern char   *cplus_sc_strtok_s(char *, rsize_t *, const char *, char **);

extern void cplus_safe_set_constraint_mode(int);
extern void cplus_sc_set_mem_constraint_handler_s_wrapper(void (*)(const char *, void *, int));
extern void cplus_sc_set_str_constraint_handler_s_wrapper(void (*)(const char *, void *, int));

static void noop(const char *m, void *p, int e) { (void)m;(void)p;(void)e; }

#define HUGE ((rsize_t)(SIZE_MAX >> 1)) /* RSIZE_MAX_MEM / RSIZE_MAX_STR */

enum { P_NULLSRC_HUGEDMAX_MEMCPY, P_OVERLAP_HUGEDMAX_MEMCPY,
       P_UNTERM_HUGEDMAX_STRCAT, P_NULLSRC_SMALLDMAX_MEMCPY,
       P_HUGEDMAX_SMALLN_MEMCPY, P_NULLSRC_HUGEDMAX_MEMSET,
       P_HUGEDMAX_STRCAT, P_N };

static void child(int p) {
    unsigned char buf[256 + 64];
    int rc = -999;
    size_t i;
    cplus_safe_set_constraint_mode(0);
    cplus_sc_set_mem_constraint_handler_s_wrapper(noop);
    cplus_sc_set_str_constraint_handler_s_wrapper(noop);
    alarm(3);
    for (i = 0; i < sizeof buf; i++) buf[i] = 0xC3;
    switch (p) {
    case P_NULLSRC_HUGEDMAX_MEMCPY: memset(buf, 0x41, 4); rc = cplus_sc_memcpy_s(buf, HUGE, NULL, 4); break;
    case P_OVERLAP_HUGEDMAX_MEMCPY: memset(buf, 0x41, 8); rc = cplus_sc_memcpy_s(buf, HUGE, buf + 2, 4); break;
    case P_UNTERM_HUGEDMAX_STRCAT: memset(buf, 0x41, 256); rc = cplus_sc_strcat_s((char*)buf, HUGE, "XY"); break;
    case P_NULLSRC_SMALLDMAX_MEMCPY: rc = cplus_sc_memcpy_s(buf, 256, NULL, 4); break;
    case P_HUGEDMAX_SMALLN_MEMCPY: rc = cplus_sc_memcpy_s(buf, HUGE, "abcd", 4); break;
    case P_NULLSRC_HUGEDMAX_MEMSET: rc = cplus_sc_memset_s(buf, HUGE, 0x41, 4); break;
    case P_HUGEDMAX_STRCAT: buf[0]=0; rc = cplus_sc_strcat_s((char*)buf, HUGE, "XY"); break;
    default: _exit(90);
    }
    for (i = 256; i < 256 + 64; i++) if (buf[i] != 0xC3) { printf("PROBE p=%d CORRUPT at %zu rc=%d\n", p, i, rc); fflush(stdout); _exit(1); }
    printf("PROBE p=%d rc=%d canary=OK\n", p, rc);
    fflush(stdout);
    _exit(0);
}

int main(void) {
    int p;
    struct rlimit rl = {0,0};
    setrlimit(RLIMIT_CORE, &rl);
    for (p = 0; p < P_N; p++) {
        pid_t pid = fork();
        int st;
        if (pid == 0) child(p);
        waitpid(pid, &st, 0);
        if (WIFSIGNALED(st)) printf("PROBE p=%d CRASH signal=%d\n", p, WTERMSIG(st));
        else if (WEXITSTATUS(st) == 1) { /* printed corrupt */ }
        else if (WEXITSTATUS(st) != 0) printf("PROBE p=%d CHILD_EXIT=%d\n", p, WEXITSTATUS(st));
        fflush(stdout);
    }
    return 0;
}
