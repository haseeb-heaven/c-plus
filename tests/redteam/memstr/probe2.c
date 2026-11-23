/* Probe 2: accepted dmax values (> actual buffer, <= RSIZE_MAX_MEM=16384)
 * combined with a self-detected constraint violation. */
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
extern errno_t cplus_sc_strcat_s(char *, rsize_t, const char *);
extern void cplus_safe_set_constraint_mode(int);
extern void cplus_sc_set_mem_constraint_handler_s_wrapper(void (*)(const char *, void *, int));
extern void cplus_sc_set_str_constraint_handler_s_wrapper(void (*)(const char *, void *, int));
static void noop(const char *m, void *p, int e) { (void)m;(void)p;(void)e; }

#define CAP 256
#define GUARD 64

static void child(int which, rsize_t dmax) {
    unsigned char buf[CAP + GUARD];
    int rc = -999;
    size_t i;
    unsigned corrupt = 0;
    cplus_safe_set_constraint_mode(0);
    cplus_sc_set_mem_constraint_handler_s_wrapper(noop);
    cplus_sc_set_str_constraint_handler_s_wrapper(noop);
    alarm(3);
    for (i = 0; i < sizeof buf; i++) buf[i] = 0xC3;
    if (which == 0) { /* null src memcpy */
        rc = cplus_sc_memcpy_s(buf, dmax, NULL, 4);
    } else if (which == 1) { /* overlap memcpy */
        memset(buf, 0x41, 8);
        rc = cplus_sc_memcpy_s(buf, dmax, buf + 2, 4);
    } else { /* unterminated strcat */
        memset(buf, 0x41, CAP);
        rc = cplus_sc_strcat_s((char*)buf, dmax, "XY");
    }
    for (i = CAP; i < CAP + GUARD; i++) if (buf[i] != 0xC3) { corrupt = 1; break; }
    printf("P which=%d dmax=%zu rc=%d canary=%s\n", which, dmax, rc, corrupt?"CORRUPT":"OK");
    fflush(stdout);
    _exit(corrupt ? 1 : 0);
}

int main(void) {
    static const rsize_t dm[] = { 257, 300, 512, 1024, 4096, 8192, 16384 };
    int w, k;
    struct rlimit rl = {0,0};
    setrlimit(RLIMIT_CORE, &rl);
    for (w = 0; w < 3; w++) for (k = 0; k < (int)(sizeof dm/sizeof dm[0]); k++) {
        pid_t pid = fork(); int st;
        if (pid == 0) child(w, dm[k]);
        waitpid(pid, &st, 0);
        if (WIFSIGNALED(st)) printf("P which=%d dmax=%zu CRASH signal=%d\n", w, dm[k], WTERMSIG(st));
        fflush(stdout);
    }
    return 0;
}
