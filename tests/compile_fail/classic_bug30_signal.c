/* Bug 30: unsafe call in a signal handler. */
#include <signal.h>
#include <stdio.h>
void handler(int sig) { (void)sig; printf("caught\n"); }
int main(void) { signal(SIGINT, handler); return 0; }
