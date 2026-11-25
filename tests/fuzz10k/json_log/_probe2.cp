#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

#define MEMCAP ((rlim_t)256u * 1024u * 1024u)
int main(void) {
    pid_t pid = fork();
    int status = 0;
    if (pid == 0) {
        struct rlimit rl;
        rl.rlim_cur = MEMCAP; rl.rlim_max = MEMCAP;
        (void)setrlimit(RLIMIT_AS, &rl);
        alarm(1);
        for (;;) { /* spin to trigger watchdog */ }
    }
    waitpid(pid, &status, 0);
    printf("signaled=%d sig=%d (SIGALRM=%d)\n", WIFSIGNALED(status),
           WIFSIGNALED(status) ? WTERMSIG(status) : -1, SIGALRM);
    return 0;
}
