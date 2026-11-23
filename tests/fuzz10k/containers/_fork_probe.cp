#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"

int main(void) {
    pid_t pid = fork();
    if (pid == 0) {
        /* child */
        _exit(0);
    } else if (pid > 0) {
        int status = 0;
        pid_t r = waitpid(pid, &status, 0);
        print("parent", r, WIFEXITED(status), WEXITSTATUS(status));
    } else {
        print("fork_failed");
        return 1;
    }
    return 0;
}
