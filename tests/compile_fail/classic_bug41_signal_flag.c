/* Bug 41: signal-modified flag without volatile sig_atomic_t. */
#include <signal.h>
#include <stdio.h>
int stop_flag = 0;
void on_signal(int sig) { (void)sig; stop_flag = 1; }
int main(void) { signal(SIGINT, on_signal); while (!stop_flag) {} printf("done\n"); return 0; }
