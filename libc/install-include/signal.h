#ifndef CPLUS_LIBC_SIGNAL_H
#define CPLUS_LIBC_SIGNAL_H
#include <signal.h>
#ifdef __cplusplus
extern "C" {
#endif
void (*signal(int sig, void (*func)(int)))(int);
int raise(int sig);
#ifdef __cplusplus
}
#endif
#endif
