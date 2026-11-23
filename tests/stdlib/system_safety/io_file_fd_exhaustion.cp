/* io_file_fd_exhaustion -- Task 3 Phase 2: fd exhaustion in a forked child.
 *
 * The child lowers its own RLIMIT_NOFILE, holds /dev/null streams until fopen
 * fails, then drives stdlib/file.hp through the failed-init and cleanup paths
 * while no descriptor is available.  The wrapper must degrade recoverably:
 * is_open()==false, native()==NULL, every operation returns a defined failure,
 * and drop()/destructor must close nothing.
 *
 * Bounded and observable:
 *   - the child installs an 8-second SIGALRM watchdog that _exit(99)s;
 *   - the held-FILE array is a fixed 64 entries;
 *   - the parent uses waitpid() and the shell applies `timeout 30`.
 * stdout is flushed before fork so the child's buffered output is not
 * duplicated.  No variable division/modulo or pointer-parameter subscripts;
 * out-params initialised; unique prefixes.
 *
 * Final line is "IO_FILE_FD_EXHAUSTION <checks> <failures>".
 */
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

static int ifde_checks = 0;
static int ifde_failures = 0;

static void ifde_check(const char *tag, int cond) {
    ifde_checks++;
    if (!cond)
        ifde_failures++;
    print(tag, cond ? 1 : 0);
}

static void ifde_alarm_handler(int sig) {
    (void)sig;
    _exit(99);
}

int main(void) {
    error ifde_err = error();
    char ifde_buf[16];
    int ifde_status = -1;
    pid_t ifde_pid;
    struct rlimit ifde_rl;

    /* Ensure the parent's stdout buffer is empty at fork time. */
    fflush(stdout);
    ifde_pid = fork();
    if (ifde_pid < 0) {
        print("fork-unavailable", 0);
        print("IO_FILE_FD_EXHAUSTION", ifde_checks, 1);
        return 1;
    }

    if (ifde_pid == 0) {
        /* ---- child ---------------------------------------------------- */
        FILE *ifde_held[64];
        int ifde_i;
        int ifde_opened = 0;
        int ifde_st;
        signal(SIGALRM, ifde_alarm_handler);
        alarm(8);

        if (getrlimit(RLIMIT_NOFILE, &ifde_rl) != 0) {
            print("getrlimit", 0);
            fflush(stdout);
            _exit(1);
        }
        if (ifde_rl.rlim_max < 16)
            ifde_rl.rlim_cur = ifde_rl.rlim_max;
        else
            ifde_rl.rlim_cur = 16;
        ifde_check("setrlimit", setrlimit(RLIMIT_NOFILE, &ifde_rl) == 0);

        for (ifde_i = 0; ifde_i < 64; ifde_i++) {
            ifde_held[ifde_i] = fopen("/dev/null", "r");
            if (ifde_held[ifde_i] == NULL)
                break;
            ifde_opened++;
        }
        ifde_check("exhausted-fopen", ifde_opened >= 1 && ifde_opened < 64);

        /* Named file whose fopen must fail under exhaustion. */
        {
            file ifde_f = file("cplus_ifde_no_such_zz", "w");
            ifde_check("exh-open-false", !ifde_f.is_open());
            ifde_check("exh-native-null", ifde_f.native() == NULL);
            memset(ifde_buf, 0x5A, sizeof(ifde_buf));
            ifde_st = ifde_f.read(ifde_buf, sizeof(ifde_buf), &ifde_err);
            ifde_check("exh-read-fail",
                       ifde_st == STDLIB_FAIL &&
                           ifde_err.category() == STDLIB_CATEGORY_IO);
            ifde_st = ifde_f.read_line(ifde_buf, sizeof(ifde_buf), &ifde_err);
            ifde_check("exh-readline-fail", ifde_st == STDLIB_FAIL);
            ifde_check("exh-write-string", ifde_f.write("x") == 0);
            ifde_st = ifde_f.write_bytes("x", 1, &ifde_err);
            ifde_check("exh-write-bytes-fail", ifde_st == STDLIB_FAIL);
            ifde_st = ifde_f.seek(0, SEEK_SET, &ifde_err);
            ifde_check("exh-seek-fail", ifde_st == STDLIB_FAIL);
            ifde_st = ifde_f.flush(&ifde_err);
            ifde_check("exh-flush-fail", ifde_st == STDLIB_FAIL);
            ifde_st = ifde_f.close(&ifde_err);
            ifde_check("exh-close-lifecycle",
                       ifde_st == STDLIB_FAIL &&
                           ifde_err.category() == STDLIB_CATEGORY_LIFECYCLE);
            ifde_f.drop();
            ifde_f.drop();
            ifde_check("exh-drop-safe", !ifde_f.is_open());
        }

        /* tmpfile() must also fail recoverably under exhaustion. */
        {
            file ifde_t = file_temp(&ifde_err);
            ifde_check("exh-temp-open-false", !ifde_t.is_open());
            ifde_check("exh-temp-error", ifde_err.failed_now());
            ifde_t.drop();
            ifde_check("exh-temp-drop-safe", !ifde_t.is_open());
        }

        /* Release the held descriptors before reporting. */
        for (ifde_i = 0; ifde_i < 64; ifde_i++) {
            if (ifde_held[ifde_i] != NULL)
                fclose(ifde_held[ifde_i]);
        }
        print("IO_FILE_FD_EXHAUSTION", ifde_checks, ifde_failures);
        fflush(stdout);
        _exit(ifde_failures == 0 ? 0 : 1);
    }

    /* ---- parent -------------------------------------------------------- */
    if (waitpid(ifde_pid, &ifde_status, 0) != ifde_pid) {
        print("waitpid", 0);
        print("IO_FILE_FD_EXHAUSTION", ifde_checks, 1);
        return 1;
    }
    ifde_check("child-normal-exit", WIFEXITED(ifde_status) ? 1 : 0);
    ifde_check("child-no-watchdog",
               WIFEXITED(ifde_status) && WEXITSTATUS(ifde_status) != 99);
    ifde_check("child-zero-failures",
               WIFEXITED(ifde_status) && WEXITSTATUS(ifde_status) == 0);
    return ifde_failures == 0 ? 0 : 1;
}
