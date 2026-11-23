/* Tiny self-test for the system_safety/support header-only helpers.
 *
 * Build (from the repo root):
 *   ./build/c+ build tests/stdlib/system_safety/support/self_test.cp \
 *       --strict --sanitize=none -o build/ss_support_self_strict
 *   ./build/c+ build tests/stdlib/system_safety/support/self_test.cp \
 *       --strict --sanitize=undefined -o build/ss_support_self_ubsan
 *
 * Exits 0 only when every check passes.  No .expected fixture is created on
 * purpose (this directory is header-owned); the exact passing stdout is
 * recorded in NOTES.md.
 */
#include "tests/stdlib/system_safety/support/watchdog.hp"
#include "tests/stdlib/system_safety/support/alloc_fail.hp"
#include "tests/stdlib/system_safety/support/fd_count.hp"
#include "tests/stdlib/system_safety/support/temp_path.hp"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>

static int self_failures;

static void self_check(const char *label, int got, int want) {
    printf("%s %d\n", label, got);
    if (got != want) {
        printf("MISMATCH %s got=%d want=%d\n", label, got, want);
        self_failures++;
    }
}

static void self_child_ok(void) {
    ss_child_exit(23);
}

static void self_child_hang(void) {
    ss_child_pause_forever();
}

int main(void) {
    struct ss_watchdog_result self_wd_ok;
    struct ss_watchdog_result self_wd_hang;
    int self_fd_base;
    int self_fd_handle;
    int self_fd_after;
    int self_fd_final;
    char self_path[128];
    char self_pid_buf[32];
    char self_pid_tag[64];
    int self_path_len;
    char self_small[8];

    /* 1. Watchdog: exact normal exit status. */
    self_wd_ok = ss_run_watchdog(self_child_ok, 3, 2);
    self_check("wd.reaped", self_wd_ok.reaped, 1);
    self_check("wd.timed_out", self_wd_ok.timed_out, 0);
    self_check("wd.exited", self_wd_ok.exited, 1);
    self_check("wd.exit_code", self_wd_ok.exit_code, 23);
    self_check("wd.raw_status", self_wd_ok.raw_status, 23 << 8);

    /* 2. Watchdog: alarm backstop kills a hanging child; status is exact. */
    self_wd_hang = ss_run_watchdog(self_child_hang, 1, 0);
    self_check("wd_hang.reaped", self_wd_hang.reaped, 1);
    self_check("wd_hang.timed_out", self_wd_hang.timed_out, 1);
    self_check("wd_hang.signaled", self_wd_hang.signaled, 1);
    self_check("wd_hang.term_signal", self_wd_hang.term_signal, SIGKILL);

    /* 3. fd-count snapshot: +1 while a descriptor is open, then restored. */
    self_fd_base = ss_fd_snapshot();
    if (self_fd_base < 0) {
        printf("fd.skip 1\n");
    } else {
        self_fd_handle = open("/dev/null", O_RDONLY);
        self_fd_after = ss_fd_snapshot();
        self_check("fd.open_ok", self_fd_handle >= 0, 1);
        self_check("fd.delta", ss_fd_delta(self_fd_base, self_fd_after), 1);
        close(self_fd_handle);
        self_fd_final = ss_fd_snapshot();
        self_check("fd.restored", ss_fd_delta(self_fd_base, self_fd_final), 0);
    }

    /* 4. Allocation-failure scope: injection is deterministic and scoped. */
    {
        alloc_fail_scope scope_at = alloc_fail_scope();
        void *scope_probe = (void *)0;
        scope_at.fail_at(1);
        scope_probe = stdlib_alloc(32);
        self_check("af.fail_at_null", scope_probe == (void *)0, 1);
        self_check("af.armed", scope_at.is_armed(), 1);
    }
    {
        void *after_probe = stdlib_alloc(32);
        self_check("af.reset_after_scope", after_probe != (void *)0, 1);
        stdlib_release(after_probe);
    }

    /* 5. PID-unique temp path + safe truncation. */
    snprintf(self_pid_buf, sizeof(self_pid_buf), "%ld", (long)getpid());
    snprintf(self_pid_tag, sizeof(self_pid_tag), "cplus-ss-%s", self_pid_buf);
    self_path_len = ss_temp_path(self_path, sizeof(self_path), "unit");
    self_check("temp.len_ok", self_path_len > 0, 1);
    self_check("temp.has_pid", strstr(self_path, self_pid_buf) != (char *)0, 1);
    self_check("temp.has_tag", strstr(self_path, "unit") != (char *)0, 1);
    memset(self_small, 'Z', sizeof(self_small));
    (void)!ss_temp_path(self_small, 4, "x");
    self_check("temp.canary", self_small[7], 'Z');

    printf("self_test done failures=%d\n", self_failures);
    return self_failures;
}
