/* Toolchain probe (temporary): validates include paths + strict mode. */
#include "stdlib/error.hp"
#include "tests/security/resource_exhaustion/support/re_harness.hp"
#include "tests/security/resource_exhaustion/support/re_limits.hp"
#include "tests/security/resource_exhaustion/support/re_watchdog.hp"

int main(void) {
    struct rlimit re_probe_saved;
    int re_probe_ok = re_limit_supported(RLIMIT_NOFILE);
    re_check("re_probe_supported", re_probe_ok == 1 || re_probe_ok == 0);
    re_check("re_probe_fd_count", re_fd_count() > 0);
    print("re_probe_fds", re_fd_count());
    {
        int i;
        for (i = 0; i < 8; i++)
            print("re_probe_fd", i, fcntl(i, F_GETFD));
    }
    (void)re_probe_saved;
    re_report();
    return re_harness_failures == 0 ? 0 : 1;
}
