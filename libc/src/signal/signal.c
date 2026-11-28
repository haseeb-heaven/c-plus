#include "../../log/log.h"
#include <signal.h>

void (*cplus_signal(int sig, void (*func)(int)))(int){ cplus_libc_log("signal","enter"); return signal(sig,func); }
int cplus_raise(int sig){ cplus_libc_log("raise","enter"); return raise(sig); }
