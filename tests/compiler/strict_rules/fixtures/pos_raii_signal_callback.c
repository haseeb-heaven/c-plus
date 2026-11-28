/* POSITIVE callback: async-signal-safe handler, volatile sig_atomic_t flag. */
#include <signal.h>
static volatile sig_atomic_t flag = 0;
static void handler(int s) { (void)s; flag = 1; }
int main(void) { signal(SIGINT, handler); return flag ? 1 : 0; }
