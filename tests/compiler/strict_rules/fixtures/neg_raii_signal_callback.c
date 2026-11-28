/* NEGATIVE callback: non-async-signal-safe call from a signal handler. */
#include <signal.h>
#include <stdio.h>
static void handler(int s) { (void)s; printf("caught\n"); }
int main(void) { signal(SIGINT, handler); return 0; }
