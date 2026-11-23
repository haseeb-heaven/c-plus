#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"

int main(void) {
    pid_t pid = fork();
    if (pid == 0) {
        raise(SIGSEGV);
        _exit(9);
    } else if (pid > 0) {
        int status = 0;
        pid_t r = waitpid(pid, &status, WNOHANG);
        print("first_poll", r, (r == 0), WIFSIGNALED(status), WTERMSIG(status));
        /* busy wait a bit */
        volatile unsigned long long i = 0;
        while (i < 200000000ULL) { i++; }
        r = waitpid(pid, &status, 0);
        print("second_poll", r, WIFSIGNALED(status), WTERMSIG(status));
    }
    return 0;
}
