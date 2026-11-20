#include <stdio.h>
#include <signal.h>
static volatile sig_atomic_t fired=0;
static void handler(int signum){ if(signum==SIGINT)fired=1; }
int main(void){ if(signal(SIGINT,handler)==SIG_ERR)return 2; printf("SAFE PRINT %d\n",(int)fired); return 0; }
